#pragma once

#include <string>
#include <string_view>
#include <memory>
#include "core/client_context.hpp"

namespace rundb::server {

/**
 * @brief High-performance client connection with sliding window read buffer
 *        and non-blocking direct write optimization.
 */
class Client {
public:
    explicit Client(int fd);
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    [[nodiscard]] int fd() const noexcept { return m_fd; }
    [[nodiscard]] core::ClientContext& context() noexcept { return m_ctx; }
    [[nodiscard]] const core::ClientContext& context() const noexcept { return m_ctx; }

    [[nodiscard]] std::string_view read_buffer() const noexcept {
        return std::string_view(m_read_buf.data() + m_read_pos, m_read_buf.size() - m_read_pos);
    }

    ssize_t read_from_socket();
    ssize_t flush_write_buffer();
    void buffer_reply(std::string_view data);
    void direct_write_or_buffer(std::string_view data);

    [[nodiscard]] bool has_pending_writes() const noexcept { return !m_write_buf.empty(); }
    void consume_read_bytes(size_t count);

    [[nodiscard]] bool is_epoll_out() const noexcept { return m_epoll_out; }
    void set_epoll_out(bool val) noexcept { m_epoll_out = val; }

private:
    int m_fd;
    core::ClientContext m_ctx;
    std::string m_read_buf;
    size_t m_read_pos{0};
    std::string m_write_buf;
    bool m_epoll_out{false};
};

using ClientPtr = std::shared_ptr<Client>;

} // namespace rundb::server
