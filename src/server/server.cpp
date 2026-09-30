#include "server/server.hpp"
#include "server/shutdown.hpp"
#include "core/aof.hpp"
#include "protocol/resp.hpp"
#include "util/printer.hpp"
#include "logger.hpp"

#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace rundb::server {

constexpr int MAX_EPOLL_EVENTS = 1024;
constexpr int CRON_INTERVAL_MS = 50;

Server::Server(std::string host, uint16_t port, core::Store& store)
    : m_host(std::move(host)), m_port(port), m_store(store) {}

Server::~Server() {
    stop();
}

void Server::set_nonblocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

bool Server::setup_listener() {
    m_listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_listen_fd < 0) {
        ERROR("SERVER", "Failed to create socket: {}", std::strerror(errno));
        return false;
    }

    int opt = 1;
    ::setsockopt(m_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ::setsockopt(m_listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    ::setsockopt(m_listen_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    set_nonblocking(m_listen_fd);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);
    if (::inet_pton(AF_INET, m_host.c_str(), &addr.sin_addr) <= 0) {
        addr.sin_addr.s_addr = INADDR_ANY;
    }

    if (::bind(m_listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ERROR("SERVER", "Failed to bind to {}:{}: {}", m_host, m_port, std::strerror(errno));
        ::close(m_listen_fd);
        m_listen_fd = -1;
        return false;
    }

    if (::listen(m_listen_fd, 4096) < 0) {
        ERROR("SERVER", "Failed to listen: {}", std::strerror(errno));
        ::close(m_listen_fd);
        m_listen_fd = -1;
        return false;
    }

    m_epoll_fd = ::epoll_create1(0);
    if (m_epoll_fd < 0) {
        ERROR("SERVER", "Failed to create epoll instance: {}", std::strerror(errno));
        ::close(m_listen_fd);
        m_listen_fd = -1;
        return false;
    }

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = m_listen_fd;
    if (::epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_listen_fd, &ev) < 0) {
        ERROR("SERVER", "Failed to add listener to epoll: {}", std::strerror(errno));
        ::close(m_listen_fd);
        ::close(m_epoll_fd);
        m_listen_fd = -1;
        m_epoll_fd = -1;
        return false;
    }

    return true;
}

bool Server::init() {
    Shutdown::setup_signals();
    return setup_listener();
}

void Server::handle_new_connection() {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = ::accept4(m_listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &addr_len, SOCK_NONBLOCK);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // Drained all incoming connections
            }
            break;
        }

        int opt = 1;
        ::setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = client_fd;
        if (::epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
            ::close(client_fd);
            continue;
        }

        m_clients[client_fd] = std::make_shared<Client>(client_fd);
        core::Stats::instance().record_connection();
    }
}

void Server::close_client(int fd) {
    ::epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    m_clients.erase(fd);
}

void Server::handle_client_read(int fd) {
    auto it = m_clients.find(fd);
    if (it == m_clients.end()) return;

    auto client = it->second;
    ssize_t bytes = client->read_from_socket();
    if (bytes <= 0 && bytes != -1) {
        close_client(fd);
        return;
    }

    // Process all fully formed RESP commands in pipeline
    while (!client->read_buffer().empty()) {
        auto [tokens, consumed] = protocol::RespParser::parse_command(client->read_buffer());
        if (consumed == 0) {
            break; // Incomplete packet, await more bytes
        }

        client->consume_read_bytes(consumed);

        if (!tokens.empty()) {
            std::string reply = m_store.process_command(client->context(), tokens);
            client->direct_write_or_buffer(reply);
        }
    }

    // Update epoll flags if write buffer has backlog
    epoll_event ev{};
    ev.data.fd = fd;
    ev.events = EPOLLIN | EPOLLET | (client->has_pending_writes() ? static_cast<uint32_t>(EPOLLOUT) : 0u);
    ::epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, fd, &ev);
}

void Server::handle_client_write(int fd) {
    auto it = m_clients.find(fd);
    if (it == m_clients.end()) return;

    auto client = it->second;
    client->flush_write_buffer();

    epoll_event ev{};
    ev.data.fd = fd;
    ev.events = EPOLLIN | EPOLLET | (client->has_pending_writes() ? static_cast<uint32_t>(EPOLLOUT) : 0u);
    ::epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, fd, &ev);
}

void Server::run() {
    rundb::Printer::print_rundb_banner(m_host, m_port);
    INFO("SERVER", "RunDB server listening on {}:{}", m_host, m_port);

    m_running = true;
    epoll_event events[MAX_EPOLL_EVENTS];

    while (m_running && !Shutdown::is_shutdown_requested()) {
        int n = ::epoll_wait(m_epoll_fd, events, MAX_EPOLL_EVENTS, CRON_INTERVAL_MS);

        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            uint32_t evs = events[i].events;

            if (fd == m_listen_fd) {
                handle_new_connection();
            } else {
                if (evs & (EPOLLERR | EPOLLHUP)) {
                    close_client(fd);
                    continue;
                }
                if (evs & EPOLLIN) {
                    handle_client_read(fd);
                }
                if (evs & EPOLLOUT) {
                    handle_client_write(fd);
                }
            }
        }

        // Active maintenance cron tick
        m_store.active_expire_cycle();
        if (auto aof = m_store.get_aof()) {
            aof->flush_if_needed();
        }
    }

    stop();
}

void Server::stop() noexcept {
    if (!m_running) return;
    m_running = false;

    int sig = Shutdown::get_signal();
    if (sig != 0) {
        rundb::Printer::print_shutdown_initiated(sig);
    }
    rundb::Printer::print_shutdown_stopping();

    m_clients.clear();
    if (m_listen_fd >= 0) {
        ::close(m_listen_fd);
        m_listen_fd = -1;
    }
    if (m_epoll_fd >= 0) {
        ::close(m_epoll_fd);
        m_epoll_fd = -1;
    }

    size_t used = core::Eviction::get_used_memory();
    size_t maxm = m_store.get_maxmemory();
    rundb::Printer::print_shutdown_complete(used, maxm);
    INFO("SERVER", "RunDB server shutting down gracefully");
}

} // namespace rundb::server
