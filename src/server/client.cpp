#include "server/client.hpp"
#include <unistd.h>
#include <sys/socket.h>
#include <cerrno>

namespace rundb::server {

Client::Client(int fd) : m_fd(fd) {
    m_read_buf.reserve(4096);
    m_write_buf.reserve(4096);
}

Client::~Client() {
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

ssize_t Client::read_from_socket() {
    char stack_buf[16384];
    ssize_t bytes = ::recv(m_fd, stack_buf, sizeof(stack_buf), 0);
    if (bytes > 0) {
        m_read_buf.append(stack_buf, static_cast<size_t>(bytes));
    }
    return bytes;
}

ssize_t Client::flush_write_buffer() {
    if (m_write_buf.empty()) return 0;

    ssize_t written = ::send(m_fd, m_write_buf.data(), m_write_buf.size(), MSG_NOSIGNAL);
    if (written > 0) {
        if (static_cast<size_t>(written) >= m_write_buf.size()) {
            m_write_buf.clear();
        } else {
            m_write_buf.erase(0, static_cast<size_t>(written));
        }
    }
    return written;
}

void Client::direct_write_or_buffer(std::string_view data) {
    if (m_write_buf.empty()) {
        ssize_t written = ::send(m_fd, data.data(), data.size(), MSG_NOSIGNAL);
        if (written >= 0 && static_cast<size_t>(written) == data.size()) {
            return; // Fast path: zero copy, entirely sent
        }
        if (written > 0) {
            data.remove_prefix(static_cast<size_t>(written));
        } else if (written < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
            return; // Socket error
        }
    }
    m_write_buf.append(data);
}

void Client::consume_read_bytes(size_t count) {
    if (count >= m_read_buf.size()) {
        m_read_buf.clear();
    } else {
        m_read_buf.erase(0, count);
    }
}

} // namespace rundb::server
