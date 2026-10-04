#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <cstdint>

namespace rundb::core {

enum class AofFsync {
    No,
    Always,
    EverySec
};

class Store;

/**
 * @brief Proprietary Append-Only File (AOF) Persistence Engine.
 *
 * DESIGN & WHY IT WORKS:
 * 1. Standard RESP Format Logging:
 *    - Commands are serialized directly into RESP wire format (*N\r\n$M\r\n...)
 *    - Makes the AOF file human-readable and compatible with standard Redis tools.
 * 2. Tunable Durability Policies:
 *    - Always: fsync() after every write (maximum safety, lower throughput).
 *    - EverySec: Background / periodic fsync() every 1 second (optimal balance).
 *    - No: OS handles page cache flushes (maximum speed).
 * 3. Startup Replayer:
 *    - Reads the AOF file on boot and feeds tokens through the Store to restore keyspace state.
 */
class AOF {
public:
    explicit AOF(std::string filename = "appendonly.aof", AofFsync fsync_policy = AofFsync::EverySec);
    ~AOF();

    bool open();
    void close();

    void log_command(const std::vector<std::string>& tokens);
    void flush_if_needed();

    // Replay on startup
    bool load_into(Store& store);

    // Snapshot state to AOF file (BGREWRITEAOF)
    bool dump_all(Store& store);

    static AofFsync parse_fsync(std::string_view policy) noexcept;

    [[nodiscard]] bool is_enabled() const noexcept { return m_enabled; }
    void set_enabled(bool enabled) noexcept { m_enabled = enabled; }
    [[nodiscard]] const std::string& filename() const noexcept { return m_filename; }
    [[nodiscard]] AofFsync fsync_policy() const noexcept { return m_policy; }
    void set_fsync_policy(AofFsync policy) noexcept { m_policy = policy; }

private:
    std::string m_filename;
    AofFsync m_policy;
    bool m_enabled{false};
    int m_fd{-1};
    uint64_t m_last_fsync_ms{0};

    void sync_to_disk();
};

} // namespace rundb::core
