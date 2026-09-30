#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <cstdint>
#include "core/store.hpp"
#include "server/client.hpp"

namespace rundb::server {

/**
 * @brief High-performance single-threaded epoll I/O multiplexing server.
 */
class Server {
public:
    Server(std::string host, uint16_t port, core::Store& store);
    ~Server();

    bool init();
    void run();
    void stop() noexcept;

private:
    std::string m_host;
    uint16_t m_port;
    core::Store& m_store;

    int m_listen_fd{-1};
    int m_epoll_fd{-1};
    bool m_running{false};

    std::unordered_map<int, ClientPtr> m_clients;

    bool setup_listener();
    void handle_new_connection();
    void handle_client_read(int fd);
    void handle_client_write(int fd);
    void close_client(int fd);
    void set_nonblocking(int fd);
};

} // namespace rundb::server
