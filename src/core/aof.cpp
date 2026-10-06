#include "core/aof.hpp"
#include "core/store.hpp"
#include "protocol/resp.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <iostream>

namespace rundb::core {

static uint64_t get_time_ms() noexcept {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

static uint64_t get_wall_time_ms() noexcept {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

AOF::AOF(std::string filename, AofFsync fsync_policy)
    : m_filename(std::move(filename)), m_policy(fsync_policy) {}

AOF::~AOF() {
    close();
}

bool AOF::open() {
    m_fd = ::open(m_filename.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (m_fd < 0) {
        m_enabled = false;
        return false;
    }
    m_enabled = true;
    m_last_fsync_ms = get_time_ms();
    return true;
}

void AOF::close() {
    if (m_fd >= 0) {
        ::fdatasync(m_fd);
        ::close(m_fd);
        m_fd = -1;
    }
    m_enabled = false;
}

void AOF::log_command(const std::vector<std::string>& tokens) {
    if (m_fd < 0 || !m_enabled || tokens.empty()) return;

    // Serialize tokens into standard RESP Array
    std::string serialized = "*" + std::to_string(tokens.size()) + "\r\n";
    for (const auto& token : tokens) {
        serialized += "$" + std::to_string(token.size()) + "\r\n";
        serialized += token + "\r\n";
    }

    size_t written = 0;
    while (written < serialized.size()) {
        ssize_t w = ::write(m_fd, serialized.data() + written, serialized.size() - written);
        if (w < 0) {
            if (errno == EINTR) continue;
            return; // Write error
        }
        written += static_cast<size_t>(w);
    }

    if (m_policy == AofFsync::Always) {
        ::fdatasync(m_fd);
    }
}

void AOF::sync_to_disk() {
    if (m_fd >= 0) {
        ::fdatasync(m_fd);
        m_last_fsync_ms = get_time_ms();
    }
}

void AOF::flush_if_needed() {
    if (m_fd < 0 || !m_enabled) return;

    if (m_policy == AofFsync::EverySec) {
        uint64_t now = get_time_ms();
        if (now - m_last_fsync_ms >= 1000) {
            sync_to_disk();
        }
    }
}

bool AOF::load_into(Store& store) {
    int read_fd = ::open(m_filename.c_str(), O_RDONLY);
    if (read_fd < 0) {
        if (errno == ENOENT) return true; // File does not exist yet (clean new instance)
        return false;
    }

    std::string buffer;
    char chunk[8192];
    ssize_t bytes_read = 0;
    while ((bytes_read = ::read(read_fd, chunk, sizeof(chunk))) > 0) {
        buffer.append(chunk, static_cast<size_t>(bytes_read));
    }
    ::close(read_fd);

    if (buffer.empty()) return true;

    // Temporarily detach any active AOF on store to avoid re-logging during replay
    auto attached_aof = store.get_aof();
    store.detach_aof();

    // Parse commands using RESP parser
    protocol::RespParser parser;
    ClientContext ctx;

    size_t offset = 0;
    while (offset < buffer.size()) {
        auto [tokens, consumed] = parser.parse_command(std::string_view(buffer.data() + offset, buffer.size() - offset));
        if (consumed == 0) {
            // Partial/truncated command at the end of AOF (crash resilience)
            break;
        }
        offset += consumed;

        if (!tokens.empty()) {
            store.process_command(ctx, tokens);
        }
    }

    if (attached_aof) {
        store.attach_aof(attached_aof);
    }

    return true;
}

AofFsync AOF::parse_fsync(std::string_view policy) noexcept {
    if (policy == "always") return AofFsync::Always;
    if (policy == "no") return AofFsync::No;
    return AofFsync::EverySec;
}

bool AOF::dump_all(Store& store) {
    std::string tmp_filename = m_filename + ".tmp";
    int tmp_fd = ::open(tmp_filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (tmp_fd < 0) return false;

    auto write_cmd = [tmp_fd](const std::vector<std::string>& tokens) -> bool {
        std::string serialized = "*" + std::to_string(tokens.size()) + "\r\n";
        for (const auto& token : tokens) {
            serialized += "$" + std::to_string(token.size()) + "\r\n";
            serialized += token + "\r\n";
        }
        size_t written = 0;
        while (written < serialized.size()) {
            ssize_t w = ::write(tmp_fd, serialized.data() + written, serialized.size() - written);
            if (w < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            written += static_cast<size_t>(w);
        }
        return true;
    };

    auto now_wall_ms = get_wall_time_ms();

    for (auto& db : store.databases()) {
        if (db.key_count() == 0) continue;

        // SELECT <db_id>
        if (!write_cmd({"SELECT", std::to_string(db.id())})) {
            ::close(tmp_fd);
            ::unlink(tmp_filename.c_str());
            return false;
        }

        for (const auto& [key, obj] : db.dict()) {
            if (db.is_expired(key)) continue;

            if (obj->type() == ObjectType::String) {
                write_cmd({"SET", key, obj->get_string_value()});
            } else if (obj->type() == ObjectType::List) {
                auto* ql = obj->get_list();
                if (ql && !ql->empty()) {
                    auto items = ql->range(0, -1);
                    std::vector<std::string> tokens = {"RPUSH", key};
                    tokens.insert(tokens.end(), items.begin(), items.end());
                    write_cmd(tokens);
                }
            } else if (obj->type() == ObjectType::Set) {
                std::vector<std::string> tokens = {"SADD", key};
                if (obj->encoding() == ObjectEncoding::IntSet) {
                    for (int64_t v : obj->get_intset()->to_vector()) {
                        tokens.push_back(std::to_string(v));
                    }
                } else if (auto* hs = obj->get_hashset()) {
                    for (const auto& member : *hs) {
                        tokens.push_back(member.to_string());
                    }
                }
                if (tokens.size() > 2) {
                    write_cmd(tokens);
                }
            }

            // Expiry preservation
            int64_t ttl_ms = db.get_ttl_ms(key);
            if (ttl_ms > 0) {
                uint64_t expire_at_sec = (now_wall_ms + static_cast<uint64_t>(ttl_ms) + 999) / 1000;
                write_cmd({"EXPIREAT", key, std::to_string(expire_at_sec)});
            }
        }
    }

    if (::fdatasync(tmp_fd) != 0) {
        ::close(tmp_fd);
        ::unlink(tmp_filename.c_str());
        return false;
    }
    ::close(tmp_fd);

    // Atomically replace the AOF file
    if (::rename(tmp_filename.c_str(), m_filename.c_str()) != 0) {
        ::unlink(tmp_filename.c_str());
        return false;
    }

    // Sync parent directory to guarantee atomic rename survives hard crash / power loss
    std::string dir_path = ".";
    auto slash_pos = m_filename.find_last_of("/\\");
    if (slash_pos != std::string::npos) {
        dir_path = m_filename.substr(0, slash_pos);
        if (dir_path.empty()) dir_path = "/";
    }
    int dir_fd = ::open(dir_path.c_str(), O_RDONLY | O_DIRECTORY);
    if (dir_fd >= 0) {
        ::fsync(dir_fd);
        ::close(dir_fd);
    }

    // Ensure main fd is open in append mode
    if (m_fd >= 0) {
        ::close(m_fd);
    }
    m_fd = ::open(m_filename.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (m_fd >= 0) {
        m_enabled = true;
    }

    return true;
}

} // namespace rundb::core
