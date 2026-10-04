#include "core/snapshot.hpp"
#include "core/store.hpp"
#include "logger.hpp"

#include <zlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>
#include <cctype>
#include <stdexcept>
#include <chrono>
#include <filesystem>
#include <algorithm>

namespace rundb::core {

namespace {

constexpr const char SNAPSHOT_MAGIC[16] = "RUNDB_SNAPSHOT\0";
constexpr uint32_t SNAPSHOT_VERSION = 1;

uint64_t get_now_ms() noexcept {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

uint64_t get_now_sec() noexcept {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

template <typename T>
void append_bytes(std::vector<uint8_t>& buf, T val) {
    uint8_t raw[sizeof(T)];
    std::memcpy(raw, &val, sizeof(T));
    buf.insert(buf.end(), raw, raw + sizeof(T));
}

void append_string(std::vector<uint8_t>& buf, std::string_view sv) {
    append_bytes<uint32_t>(buf, static_cast<uint32_t>(sv.size()));
    buf.insert(buf.end(), sv.data(), sv.data() + sv.size());
}

template <typename T>
bool read_bytes(const uint8_t*& ptr, const uint8_t* end, T& out) {
    if (ptr + sizeof(T) > end) return false;
    std::memcpy(&out, ptr, sizeof(T));
    ptr += sizeof(T);
    return true;
}

bool read_string(const uint8_t*& ptr, const uint8_t* end, std::string& out) {
    uint32_t len = 0;
    if (!read_bytes<uint32_t>(ptr, end, len)) return false;
    if (ptr + len > end) return false;
    out.assign(reinterpret_cast<const char*>(ptr), len);
    ptr += len;
    return true;
}

std::string trim(std::string_view sv) {
    auto start = sv.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return "";
    auto end = sv.find_last_not_of(" \t\r\n");
    return std::string(sv.substr(start, end - start + 1));
}

} // namespace

SnapshotManager::SnapshotManager(std::string filepath, uint64_t interval_sec)
    : m_filepath(std::move(filepath)),
      m_interval_sec(interval_sec),
      m_last_save_time(get_now_sec()) {}

SnapshotManager::~SnapshotManager() {
    wait_for_bg_save();
}

void SnapshotManager::wait_for_bg_save() {
    if (m_bg_thread.joinable()) {
        m_bg_thread.join();
    }
}

bool SnapshotManager::exists() const noexcept {
    std::error_code ec;
    return std::filesystem::exists(m_filepath, ec);
}

uint64_t SnapshotManager::parse_interval(std::string_view str) {
    std::string s = trim(str);
    if (s.empty() || s == "0" || s == "none" || s == "disabled" || s == "off") {
        return 0;
    }

    uint64_t multiplier = 1;
    char unit = s.back();
    if (unit == 's' || unit == 'S') {
        multiplier = 1;
        s.pop_back();
    } else if (unit == 'm' || unit == 'M') {
        multiplier = 60;
        s.pop_back();
    } else if (unit == 'h' || unit == 'H') {
        multiplier = 3600;
        s.pop_back();
    } else if (unit == 'd' || unit == 'D') {
        multiplier = 86400;
        s.pop_back();
    } else if (!std::isdigit(static_cast<unsigned char>(unit))) {
        throw std::runtime_error("Invalid snapshot interval unit: '" + std::string(str) + "'. Expected S (seconds), M (minutes), H (hours), or D (days)");
    }

    s = trim(s);
    if (s.empty()) {
        throw std::runtime_error("Missing numeric duration in snapshot interval: '" + std::string(str) + "'");
    }

    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            throw std::runtime_error("Invalid non-numeric value in snapshot interval: '" + std::string(str) + "'");
        }
    }

    try {
        uint64_t val = std::stoull(s);
        return val * multiplier;
    } catch (...) {
        throw std::runtime_error("Snapshot interval value out of range: '" + std::string(str) + "'");
    }
}

std::vector<SnapshotManager::ExtractedDb> SnapshotManager::extract_store_data(const Store& store) {
    std::vector<ExtractedDb> extracted;
    uint64_t current_time = get_now_ms();

    for (size_t db_idx = 0; db_idx < store.db_count(); ++db_idx) {
        const auto& db = store.get_db(static_cast<int>(db_idx));
        if (db.key_count() == 0) continue;

        ExtractedDb edb;
        edb.id = static_cast<int>(db_idx);

        for (const auto& [k, obj] : db.dict()) {
            if (!obj) continue;

            int64_t ttl_ms = db.get_ttl_ms_const(k, current_time);
            if (ttl_ms == -2) continue; // expired or missing

            uint64_t expire_at_ms = 0;
            if (ttl_ms > 0) {
                expire_at_ms = current_time + static_cast<uint64_t>(ttl_ms);
            }

            ExtractedKey ekey;
            ekey.key = k;
            ekey.expire_at_ms = expire_at_ms;

            if (obj->type() == ObjectType::String) {
                ekey.type = 0;
                ekey.string_val = obj->get_string_value();
            } else if (obj->type() == ObjectType::List) {
                ekey.type = 1;
                auto* ql = obj->get_list();
                if (ql) {
                    ekey.list_or_set_vals = ql->range(0, -1);
                }
            } else if (obj->type() == ObjectType::Set) {
                ekey.type = 2;
                if (obj->encoding() == ObjectEncoding::IntSet) {
                    for (int64_t v : obj->get_intset()->to_vector()) {
                        ekey.list_or_set_vals.push_back(std::to_string(v));
                    }
                } else if (auto* hs = obj->get_hashset()) {
                    for (const auto& item : *hs) {
                        ekey.list_or_set_vals.push_back(item.to_string());
                    }
                }
            }

            edb.keys.push_back(std::move(ekey));
        }

        if (!edb.keys.empty()) {
            extracted.push_back(std::move(edb));
        }
    }

    return extracted;
}

bool SnapshotManager::write_snapshot_file(const std::string& target_file, const std::vector<ExtractedDb>& data) {
    // 1. Serialize extracted data into uncompressed binary buffer
    std::vector<uint8_t> uncompressed_buf;
    uncompressed_buf.reserve(64 * 1024);

    append_bytes<uint32_t>(uncompressed_buf, static_cast<uint32_t>(data.size()));

    for (const auto& db : data) {
        append_bytes<uint32_t>(uncompressed_buf, static_cast<uint32_t>(db.id));
        append_bytes<uint32_t>(uncompressed_buf, static_cast<uint32_t>(db.keys.size()));

        for (const auto& k : db.keys) {
            append_string(uncompressed_buf, k.key);
            append_bytes<uint8_t>(uncompressed_buf, k.type);
            append_bytes<uint64_t>(uncompressed_buf, k.expire_at_ms);

            if (k.type == 0) { // String
                append_string(uncompressed_buf, k.string_val);
            } else if (k.type == 1 || k.type == 2) { // List or Set
                append_bytes<uint32_t>(uncompressed_buf, static_cast<uint32_t>(k.list_or_set_vals.size()));
                for (const auto& item : k.list_or_set_vals) {
                    append_string(uncompressed_buf, item);
                }
            }
        }
    }

    // Trailing EOF marker
    append_bytes<uint8_t>(uncompressed_buf, 0xFF);

    // 2. Calculate CRC32 checksum of uncompressed content
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, uncompressed_buf.data(), static_cast<uInt>(uncompressed_buf.size()));

    // 3. Compress buffer using zlib DEFLATE
    uLong uncompressed_len = static_cast<uLong>(uncompressed_buf.size());
    uLong bound = compressBound(uncompressed_len);
    std::vector<uint8_t> compressed_buf(bound);
    uLong compressed_len = bound;

    int z_res = compress(compressed_buf.data(), &compressed_len, uncompressed_buf.data(), uncompressed_len);
    if (z_res != Z_OK) {
        ERROR("SNAPSHOT", "Failed to compress snapshot buffer: zlib error code {}", z_res);
        return false;
    }
    compressed_buf.resize(compressed_len);

    // 4. Ensure parent directories exist
    std::filesystem::path target_path(target_file);
    if (target_path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(target_path.parent_path(), ec);
    }

    // 5. Write header + compressed payload to temporary file
    std::string tmp_file = target_file + ".tmp";
    int fd = ::open(tmp_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        ERROR("SNAPSHOT", "Failed to open temporary snapshot file '{}': {}", tmp_file, std::strerror(errno));
        return false;
    }

    std::vector<uint8_t> header;
    header.reserve(40);
    header.insert(header.end(), SNAPSHOT_MAGIC, SNAPSHOT_MAGIC + sizeof(SNAPSHOT_MAGIC));
    append_bytes<uint32_t>(header, SNAPSHOT_VERSION);
    append_bytes<uint64_t>(header, static_cast<uint64_t>(uncompressed_len));
    append_bytes<uint64_t>(header, static_cast<uint64_t>(compressed_len));
    append_bytes<uint32_t>(header, static_cast<uint32_t>(crc));

    auto write_all = [fd](const uint8_t* p, size_t sz) -> bool {
        size_t off = 0;
        while (off < sz) {
            ssize_t w = ::write(fd, p + off, sz - off);
            if (w < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            off += static_cast<size_t>(w);
        }
        return true;
    };

    if (!write_all(header.data(), header.size()) || !write_all(compressed_buf.data(), compressed_buf.size())) {
        ERROR("SNAPSHOT", "Failed writing data to temporary snapshot file '{}'", tmp_file);
        ::close(fd);
        ::unlink(tmp_file.c_str());
        return false;
    }

    if (::fdatasync(fd) != 0) {
        ERROR("SNAPSHOT", "Failed fsync on temporary snapshot file '{}'", tmp_file);
        ::close(fd);
        ::unlink(tmp_file.c_str());
        return false;
    }
    ::close(fd);

    // 6. Atomically rename temporary file to destination
    if (::rename(tmp_file.c_str(), target_file.c_str()) != 0) {
        ERROR("SNAPSHOT", "Failed atomic rename from '{}' to '{}': {}", tmp_file, target_file, std::strerror(errno));
        ::unlink(tmp_file.c_str());
        return false;
    }

    // 7. Fsync parent directory
    std::string dir_path = ".";
    auto slash = target_file.find_last_of("/\\");
    if (slash != std::string::npos) {
        dir_path = target_file.substr(0, slash);
        if (dir_path.empty()) dir_path = "/";
    }
    int dir_fd = ::open(dir_path.c_str(), O_RDONLY | O_DIRECTORY);
    if (dir_fd >= 0) {
        ::fsync(dir_fd);
        ::close(dir_fd);
    }

    double ratio = uncompressed_len > 0 ? (1.0 - (static_cast<double>(compressed_len) / static_cast<double>(uncompressed_len))) * 100.0 : 0.0;
    INFO("SNAPSHOT", "DB snapshot saved: {} bytes raw -> {} bytes compressed ({:.1f}% space saved) to '{}'",
         uncompressed_len, compressed_len, ratio, target_file);
    return true;
}

bool SnapshotManager::save(const Store& store) {
    auto start_tp = std::chrono::steady_clock::now();
    auto data = extract_store_data(store);
    bool ok = write_snapshot_file(m_filepath, data);
    auto end_tp = std::chrono::steady_clock::now();
    int64_t dur_s = std::chrono::duration_cast<std::chrono::seconds>(end_tp - start_tp).count();

    m_last_save_time.store(get_now_sec());
    m_last_bgsave_ok.store(ok);
    m_last_bgsave_time_sec.store(dur_s);
    m_changes_since_last_save.store(0);
    return ok;
}

bool SnapshotManager::bgsave(const Store& store) {
    if (m_bgsave_in_progress.exchange(true)) {
        return false; // Already in progress
    }

    wait_for_bg_save();

    // Instantaneous point-in-time state extraction on calling thread
    auto data = extract_store_data(store);

    m_bg_thread = std::thread([this, data = std::move(data)]() {
        auto start_tp = std::chrono::steady_clock::now();
        bool ok = write_snapshot_file(m_filepath, data);
        auto end_tp = std::chrono::steady_clock::now();
        int64_t dur_s = std::chrono::duration_cast<std::chrono::seconds>(end_tp - start_tp).count();

        m_last_save_time.store(get_now_sec());
        m_last_bgsave_ok.store(ok);
        m_last_bgsave_time_sec.store(dur_s);
        m_changes_since_last_save.store(0);
        m_bgsave_in_progress.store(false);
    });

    return true;
}

size_t SnapshotManager::load_into(Store& store) {
    int fd = ::open(m_filepath.c_str(), O_RDONLY);
    if (fd < 0) {
        return 0;
    }

    struct stat st{};
    if (::fstat(fd, &st) != 0 || st.st_size < 40) {
        ::close(fd);
        return 0;
    }

    std::vector<uint8_t> file_data(static_cast<size_t>(st.st_size));
    size_t off = 0;
    while (off < file_data.size()) {
        ssize_t r = ::read(fd, file_data.data() + off, file_data.size() - off);
        if (r < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            return 0;
        }
        if (r == 0) break;
        off += static_cast<size_t>(r);
    }
    ::close(fd);

    if (off < 40) return 0;

    const uint8_t* ptr = file_data.data();
    const uint8_t* end = file_data.data() + off;

    // Validate magic
    if (std::memcmp(ptr, SNAPSHOT_MAGIC, sizeof(SNAPSHOT_MAGIC)) != 0) {
        ERROR("SNAPSHOT", "Invalid snapshot magic in file '{}'", m_filepath);
        return 0;
    }
    ptr += sizeof(SNAPSHOT_MAGIC);

    uint32_t version = 0;
    uint64_t uncompressed_size = 0;
    uint64_t compressed_size = 0;
    uint32_t expected_crc = 0;

    if (!read_bytes<uint32_t>(ptr, end, version) ||
        !read_bytes<uint64_t>(ptr, end, uncompressed_size) ||
        !read_bytes<uint64_t>(ptr, end, compressed_size) ||
        !read_bytes<uint32_t>(ptr, end, expected_crc)) {
        ERROR("SNAPSHOT", "Truncated header in snapshot file '{}'", m_filepath);
        return 0;
    }

    if (version != SNAPSHOT_VERSION) {
        ERROR("SNAPSHOT", "Unsupported snapshot version {} in file '{}'", version, m_filepath);
        return 0;
    }

    if (static_cast<size_t>(end - ptr) < compressed_size) {
        ERROR("SNAPSHOT", "Snapshot file '{}' is truncated", m_filepath);
        return 0;
    }

    // Decompress payload
    std::vector<uint8_t> uncompressed_buf(uncompressed_size);
    uLong dest_len = static_cast<uLong>(uncompressed_size);
    int z_res = uncompress(uncompressed_buf.data(), &dest_len, ptr, static_cast<uLong>(compressed_size));
    if (z_res != Z_OK || dest_len != uncompressed_size) {
        ERROR("SNAPSHOT", "Snapshot decompression failed for file '{}': code {}", m_filepath, z_res);
        return 0;
    }

    // Verify CRC32 checksum
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, uncompressed_buf.data(), static_cast<uInt>(dest_len));
    if (static_cast<uint32_t>(crc) != expected_crc) {
        ERROR("SNAPSHOT", "Snapshot CRC32 checksum mismatch in file '{}': expected {:#x}, got {:#x}",
              m_filepath, expected_crc, static_cast<uint32_t>(crc));
        return 0;
    }

    // Parse keyspace objects
    const uint8_t* u_ptr = uncompressed_buf.data();
    const uint8_t* u_end = uncompressed_buf.data() + dest_len;

    uint32_t num_dbs = 0;
    if (!read_bytes<uint32_t>(u_ptr, u_end, num_dbs)) return 0;

    size_t restored_keys = 0;
    uint64_t current_ms = get_now_ms();

    for (uint32_t d = 0; d < num_dbs; ++d) {
        uint32_t db_id = 0;
        uint32_t num_keys = 0;
        if (!read_bytes<uint32_t>(u_ptr, u_end, db_id) ||
            !read_bytes<uint32_t>(u_ptr, u_end, num_keys)) {
            break;
        }

        Database& db = store.get_db(static_cast<int>(db_id));

        for (uint32_t k_idx = 0; k_idx < num_keys; ++k_idx) {
            std::string key;
            uint8_t type = 0;
            uint64_t expire_at_ms = 0;

            if (!read_string(u_ptr, u_end, key) ||
                !read_bytes<uint8_t>(u_ptr, u_end, type) ||
                !read_bytes<uint64_t>(u_ptr, u_end, expire_at_ms)) {
                break;
            }

            bool is_expired = (expire_at_ms > 0 && expire_at_ms <= current_ms);

            if (type == 0) { // String
                std::string val;
                if (!read_string(u_ptr, u_end, val)) break;
                if (!is_expired) {
                    auto obj = RunDBObject::create_string(val);
                    db.set(key, std::move(obj));
                    restored_keys++;
                }
            } else if (type == 1) { // List
                uint32_t num_items = 0;
                if (!read_bytes<uint32_t>(u_ptr, u_end, num_items)) break;
                auto obj = RunDBObject::create_list();
                auto* ql = obj->get_list();
                for (uint32_t i = 0; i < num_items; ++i) {
                    std::string item;
                    if (!read_string(u_ptr, u_end, item)) break;
                    if (ql) ql->push_back(item);
                }
                if (!is_expired) {
                    db.set(key, std::move(obj));
                    restored_keys++;
                }
            } else if (type == 2) { // Set
                uint32_t num_members = 0;
                if (!read_bytes<uint32_t>(u_ptr, u_end, num_members)) break;
                auto obj = RunDBObject::create_set();
                for (uint32_t i = 0; i < num_members; ++i) {
                    std::string member;
                    if (!read_string(u_ptr, u_end, member)) break;
                    obj->set_add(member);
                }
                if (!is_expired) {
                    db.set(key, std::move(obj));
                    restored_keys++;
                }
            }

            // Expiry restoration
            if (!is_expired && expire_at_ms > 0) {
                db.set_expire(key, expire_at_ms);
            }
        }
    }

    INFO("SNAPSHOT", "Point-in-time recovery complete: loaded {} keys across {} databases from '{}'",
         restored_keys, num_dbs, m_filepath);
    return restored_keys;
}

void SnapshotManager::cron_tick(Store& store) {
    // Reap finished background thread
    if (m_bg_thread.joinable() && !m_bgsave_in_progress.load()) {
        m_bg_thread.join();
    }

    if (m_interval_sec == 0 || m_bgsave_in_progress.load()) {
        return;
    }

    uint64_t now_sec = get_now_sec();
    if (now_sec - m_last_save_time.load() >= m_interval_sec) {
        INFO("SNAPSHOT", "Periodic snapshot timer fired (interval: {}s). Initiating BGSAVE...", m_interval_sec);
        bgsave(store);
    }
}

} // namespace rundb::core
