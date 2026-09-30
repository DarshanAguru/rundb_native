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

    [[nodiscard]] std::string& read_buffer() noexcept { return m_read_buf; }
    [[nodiscard]] const std::string& read_buffer() const noexcept { return m_read_buf; }

    ssize_t read_from_socket();
    ssize_t flush_write_buffer();
    void direct_write_or_buffer(std::string_view data);

    [[nodiscard]] bool has_pending_writes() const noexcept { return !m_write_buf.empty(); }
    void consume_read_bytes(size_t count);

private:
    int m_fd;
    core::ClientContext m_ctx;
    std::string m_read_buf;
    std::string m_write_buf;
};

using ClientPtr = std::shared_ptr<Client>;

} // namespace rundb::server
