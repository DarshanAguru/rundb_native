#include "config.hpp"
#include "argparse.hpp"
#include "core/snapshot.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {
    std::string trim(const std::string& str) {
        auto start = str.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        auto end = str.find_last_not_of(" \t\r\n");
        return str.substr(start, end - start + 1);
    }

    std::string normalize_key(std::string str) {
        for (char& c : str) {
            if (c == '-') c = '_';
            else c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        return str;
    }

    bool str_ends_with(std::string_view s, std::string_view suffix) {
        return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
    }

    size_t parse_memory_bytes(std::string s) {
        s = trim(s);
        if (s.empty()) return 0;
        size_t multiplier = 1;
        std::string lower = s;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (str_ends_with(lower, "gb") || str_ends_with(lower, "g")) {
            multiplier = 1024ULL * 1024ULL * 1024ULL;
            lower.erase(lower.find_last_not_of("gb") + 1);
        } else if (str_ends_with(lower, "mb") || str_ends_with(lower, "m")) {
            multiplier = 1024ULL * 1024ULL;
            lower.erase(lower.find_last_not_of("mb") + 1);
        } else if (str_ends_with(lower, "kb") || str_ends_with(lower, "k")) {
            multiplier = 1024ULL;
            lower.erase(lower.find_last_not_of("kb") + 1);
        } else if (str_ends_with(lower, "b")) {
            lower.pop_back();
        }
        try {
            return static_cast<size_t>(std::stoull(lower) * multiplier);
        } catch (...) {
            return 0;
        }
    }
} // namespace

namespace rundb {

void Config::load(Args& opts) {
    Args cli = opts; // Snapshot explicit CLI flags

    // 1. Load from file if specified or present
    bool file_required = cli.has_config_path;
    load_from_file(opts.config_path.c_str(), opts, file_required);

    // 2. Load from Environment Variables (higher precedence than file)
    if (const char* v = std::getenv("RUNDB_HOST")) opts.host = v;
    if (const char* v = std::getenv("RUNDB_PORT")) {
        try { opts.port = static_cast<uint16_t>(std::stoi(v)); } catch (...) {}
    }
    if (const char* v = std::getenv("RUNDB_LOG_LEVEL")) opts.log_level = v;
    if (const char* v = std::getenv("RUNDB_MEMORY_LIMIT")) {
        opts.memory_limit = parse_memory_bytes(v);
    }
    if (const char* v = std::getenv("RUNDB_MAX_CLIENTS")) {
        try { opts.max_clients = static_cast<size_t>(std::stoull(v)); } catch (...) {}
    }
    if (const char* v = std::getenv("RUNDB_CRON_FREQ_INTERVAL")) {
        try { opts.cron_freq_interval = std::stod(v); } catch (...) {}
    }
    if (const char* v = std::getenv("RUNDB_AOF_FILE")) opts.aof_file = v;
    if (const char* v = std::getenv("RUNDB_AOF_ENABLED")) {
        std::string s(v);
        opts.aof_enabled = (s == "yes" || s == "true" || s == "1");
    }
    if (const char* v = std::getenv("RUNDB_AOF_FSYNC")) opts.aof_fsync = v;
    if (const char* v = std::getenv("RUNDB_EVICTION_STRATEGY")) opts.eviction_strategy = v;
    if (const char* v = std::getenv("RUNDB_EVICTION_RATIO")) {
        try { opts.eviction_ratio = std::stod(v); } catch (...) {}
    }
    if (const char* v = std::getenv("RUNDB_DB_COUNT")) {
        try { opts.db_count = static_cast<size_t>(std::stoull(v)); } catch (...) {}
    }
    if (const char* v = std::getenv("RUNDB_EVICTION_POOL_SIZE")) {
        try { opts.eviction_pool_size = static_cast<size_t>(std::stoull(v)); } catch (...) {}
    }
    if (const char* v = std::getenv("RUNDB_EVICTION_SAMPLE_SIZE")) {
        try { opts.eviction_sample_size = static_cast<size_t>(std::stoull(v)); } catch (...) {}
    }
    if (const char* v = std::getenv("RUNDB_SNAPSHOT_INTERVAL")) {
        try {
            opts.snapshot_interval_str = v;
            opts.snapshot_interval_sec = core::SnapshotManager::parse_interval(v);
        } catch (...) {}
    }
    if (const char* v = std::getenv("RUNDB_SNAPSHOT_FILE")) opts.snapshot_file = v;

    // 3. CLI arguments take HIGHEST precedence over file and env vars
    if (cli.has_port) opts.port = cli.port;
    if (cli.has_host) opts.host = cli.host;
    if (cli.has_log_level) opts.log_level = cli.log_level;
    if (cli.has_config_path) opts.config_path = cli.config_path;
    if (cli.has_memory_limit) opts.memory_limit = cli.memory_limit;
    if (cli.has_max_clients) opts.max_clients = cli.max_clients;
    if (cli.has_cron_freq_interval) opts.cron_freq_interval = cli.cron_freq_interval;
    if (cli.has_aof_file) opts.aof_file = cli.aof_file;
    if (cli.has_aof_enabled) opts.aof_enabled = cli.aof_enabled;
    if (cli.has_aof_fsync) opts.aof_fsync = cli.aof_fsync;
    if (cli.has_eviction_strategy) opts.eviction_strategy = cli.eviction_strategy;
    if (cli.has_eviction_ratio) opts.eviction_ratio = cli.eviction_ratio;
    if (cli.has_db_count) opts.db_count = cli.db_count;
    if (cli.has_eviction_pool_size) opts.eviction_pool_size = cli.eviction_pool_size;
    if (cli.has_eviction_sample_size) opts.eviction_sample_size = cli.eviction_sample_size;
    if (cli.has_snapshot_interval) {
        opts.snapshot_interval_str = cli.snapshot_interval_str;
        opts.snapshot_interval_sec = cli.snapshot_interval_sec;
    }
    if (cli.has_snapshot_file) opts.snapshot_file = cli.snapshot_file;
}

void Config::load_from_file(const char* path, Args& opts, bool required) {
    std::ifstream file(path);

    if (!file) {
        if (required) {
            throw std::runtime_error(std::string("Failed to open configuration file: ") + path);
        }
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        std::string key, val;
        auto eq_pos = trimmed.find('=');
        if (eq_pos != std::string::npos) {
            key = trim(trimmed.substr(0, eq_pos));
            val = trim(trimmed.substr(eq_pos + 1));
        } else {
            auto space_pos = trimmed.find_first_of(" \t");
            if (space_pos != std::string::npos) {
                key = trim(trimmed.substr(0, space_pos));
                val = trim(trimmed.substr(space_pos + 1));
            } else {
                continue;
            }
        }

        // Strip quotes
        if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') || (val.front() == '\'' && val.back() == '\''))) {
            val = val.substr(1, val.size() - 2);
        }

        std::string ukey = normalize_key(key);
        try {
            if (ukey == "HOST" || ukey == "BIND") opts.host = val;
            else if (ukey == "PORT") opts.port = static_cast<uint16_t>(std::stoi(val));
            else if (ukey == "LOG_LEVEL" || ukey == "LOGLEVEL") opts.log_level = val;
            else if (ukey == "MEMORY_LIMIT" || ukey == "MAXMEMORY") opts.memory_limit = parse_memory_bytes(val);
            else if (ukey == "MAX_CLIENTS" || ukey == "MAXCLIENTS") opts.max_clients = static_cast<size_t>(std::stoull(val));
            else if (ukey == "CRON_FREQ_INTERVAL") opts.cron_freq_interval = std::stod(val);
            else if (ukey == "AOF_FILE" || ukey == "APPENDFILENAME") opts.aof_file = val;
            else if (ukey == "AOF_ENABLED" || ukey == "APPENDONLY") opts.aof_enabled = (val == "yes" || val == "true" || val == "1");
            else if (ukey == "AOF_FSYNC" || ukey == "APPENDFSYNC") opts.aof_fsync = val;
            else if (ukey == "EVICTION_STRATEGY" || ukey == "MAXMEMORY_POLICY") opts.eviction_strategy = val;
            else if (ukey == "EVICTION_RATIO") opts.eviction_ratio = std::stod(val);
            else if (ukey == "DB_COUNT" || ukey == "DATABASES") opts.db_count = static_cast<size_t>(std::stoull(val));
            else if (ukey == "EVICTION_POOL_SIZE") opts.eviction_pool_size = static_cast<size_t>(std::stoull(val));
            else if (ukey == "EVICTION_SAMPLE_SIZE") opts.eviction_sample_size = static_cast<size_t>(std::stoull(val));
            else if (ukey == "SNAPSHOT_INTERVAL" || ukey == "SNAPSHOT") {
                opts.snapshot_interval_str = val;
                opts.snapshot_interval_sec = core::SnapshotManager::parse_interval(val);
            }
            else if (ukey == "SAVE") {
                std::string s_val = trim(val);
                if (s_val.empty() || s_val == "\"\"" || s_val == "''") {
                    opts.snapshot_interval_str = "";
                    opts.snapshot_interval_sec = 0;
                } else {
                    auto sp = s_val.find_first_of(" \t");
                    std::string first_tok = (sp != std::string::npos) ? s_val.substr(0, sp) : s_val;
                    opts.snapshot_interval_str = first_tok;
                    opts.snapshot_interval_sec = core::SnapshotManager::parse_interval(first_tok);
                }
            }
            else if (ukey == "SNAPSHOT_FILE" || ukey == "DBFILENAME") opts.snapshot_file = val;
        } catch (...) {
            // Ignore malformed individual config lines during file load
        }
    }
}

} // namespace rundb