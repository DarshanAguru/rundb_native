#include "config.hpp"
#include "argparse.hpp"

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

    std::string to_upper(std::string str) {
        for (char& c : str) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return str;
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
    if (const char* v = std::getenv("RUNDB_PORT")) opts.port = static_cast<uint16_t>(std::stoi(v));
    if (const char* v = std::getenv("RUNDB_LOG_LEVEL")) opts.log_level = v;
    if (const char* v = std::getenv("RUNDB_MEMORY_LIMIT")) opts.memory_limit = static_cast<size_t>(std::stoull(v));
    if (const char* v = std::getenv("RUNDB_MAX_CLIENTS")) opts.max_clients = static_cast<size_t>(std::stoull(v));
    if (const char* v = std::getenv("RUNDB_CRON_FREQ_INTERVAL")) opts.cron_freq_interval = std::stod(v);
    if (const char* v = std::getenv("RUNDB_AOF_FILE")) opts.aof_file = v;
    if (const char* v = std::getenv("RUNDB_AOF_ENABLED")) {
        std::string s(v);
        opts.aof_enabled = (s == "yes" || s == "true" || s == "1");
    }
    if (const char* v = std::getenv("RUNDB_AOF_FSYNC")) opts.aof_fsync = v;
    if (const char* v = std::getenv("RUNDB_EVICTION_STRATEGY")) opts.eviction_strategy = v;
    if (const char* v = std::getenv("RUNDB_EVICTION_RATIO")) opts.eviction_ratio = std::stod(v);
    if (const char* v = std::getenv("RUNDB_DB_COUNT")) opts.db_count = static_cast<size_t>(std::stoull(v));
    if (const char* v = std::getenv("RUNDB_EVICTION_POOL_SIZE")) opts.eviction_pool_size = static_cast<size_t>(std::stoull(v));
    if (const char* v = std::getenv("RUNDB_EVICTION_SAMPLE_SIZE")) opts.eviction_sample_size = static_cast<size_t>(std::stoull(v));

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

        std::string ukey = to_upper(key);
        if (ukey == "HOST") opts.host = val;
        else if (ukey == "PORT") opts.port = static_cast<uint16_t>(std::stoi(val));
        else if (ukey == "LOG_LEVEL") opts.log_level = val;
        else if (ukey == "MEMORY_LIMIT" || ukey == "MAXMEMORY") opts.memory_limit = static_cast<size_t>(std::stoull(val));
        else if (ukey == "MAX_CLIENTS") opts.max_clients = static_cast<size_t>(std::stoull(val));
        else if (ukey == "CRON_FREQ_INTERVAL") opts.cron_freq_interval = std::stod(val);
        else if (ukey == "AOF_FILE") opts.aof_file = val;
        else if (ukey == "AOF_ENABLED") opts.aof_enabled = (val == "yes" || val == "true" || val == "1");
        else if (ukey == "AOF_FSYNC") opts.aof_fsync = val;
        else if (ukey == "EVICTION_STRATEGY" || ukey == "MAXMEMORY_POLICY") opts.eviction_strategy = val;
        else if (ukey == "EVICTION_RATIO") opts.eviction_ratio = std::stod(val);
        else if (ukey == "DB_COUNT") opts.db_count = static_cast<size_t>(std::stoull(val));
        else if (ukey == "EVICTION_POOL_SIZE") opts.eviction_pool_size = static_cast<size_t>(std::stoull(val));
        else if (ukey == "EVICTION_SAMPLE_SIZE") opts.eviction_sample_size = static_cast<size_t>(std::stoull(val));
    }
}

} // namespace rundb