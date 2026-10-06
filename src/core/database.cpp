#include "core/database.hpp"
#include <chrono>
#include <random>

namespace rundb::core {

uint64_t Database::now_ms() noexcept {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

void Database::set(std::string_view key, ObjectPtr val) {
    if (!m_expires.empty()) {
        auto exp_it = m_expires.find(key);
        if (exp_it != m_expires.end()) {
            m_expires.erase(exp_it);
        }
    }
    auto it = m_dict.find(key);
    if (it != m_dict.end()) {
        it->second = std::move(val);
    } else {
        m_dict.emplace(std::string(key), std::move(val));
    }
}

ObjectPtr Database::get(std::string_view key) {
    if (!m_expires.empty() && is_expired(key)) {
        del(key);
        return nullptr;
    }
    auto it = m_dict.find(key);
    if (it != m_dict.end()) {
        it->second->update_lru();
        return it->second;
    }
    return nullptr;
}

ObjectPtr Database::get_no_touch(std::string_view key) {
    if (!m_expires.empty() && is_expired(key)) {
        del(key);
        return nullptr;
    }
    auto it = m_dict.find(key);
    if (it != m_dict.end()) {
        return it->second;
    }
    return nullptr;
}

bool Database::del(std::string_view key) {
    if (!m_expires.empty()) {
        auto exp_it = m_expires.find(key);
        if (exp_it != m_expires.end()) {
            m_expires.erase(exp_it);
        }
    }
    auto it = m_dict.find(key);
    if (it != m_dict.end()) {
        m_dict.erase(it);
        return true;
    }
    return false;
}

bool Database::exists(std::string_view key) {
    return get_no_touch(key) != nullptr;
}

void Database::flush() noexcept {
    m_dict.clear();
    m_expires.clear();
}

bool Database::set_expire(std::string_view key, uint64_t expire_time_ms) {
    auto it = m_dict.find(key);
    if (it == m_dict.end()) return false;
    auto exp_it = m_expires.find(key);
    if (exp_it != m_expires.end()) {
        exp_it->second = expire_time_ms;
    } else {
        m_expires.emplace(std::string(key), expire_time_ms);
    }
    return true;
}

bool Database::persist(std::string_view key) {
    if (m_expires.empty()) return false;
    auto it = m_expires.find(key);
    if (it != m_expires.end()) {
        m_expires.erase(it);
        return true;
    }
    return false;
}

int64_t Database::get_ttl_ms(std::string_view key) {
    auto it = m_dict.find(key);
    if (it == m_dict.end()) return -2; // Key does not exist

    if (m_expires.empty()) return -1; // Key exists but has no associated expire
    auto exp_it = m_expires.find(key);
    if (exp_it == m_expires.end()) return -1;

    uint64_t current = now_ms();
    if (exp_it->second <= current) {
        del(key);
        return -2;
    }
    return static_cast<int64_t>(exp_it->second - current);
}

int64_t Database::get_ttl_ms_const(std::string_view key, uint64_t current) const {
    auto it = m_dict.find(key);
    if (it == m_dict.end()) return -2; // Key does not exist

    if (m_expires.empty()) return -1; // Key exists but has no associated expire
    auto exp_it = m_expires.find(key);
    if (exp_it == m_expires.end()) return -1;

    if (exp_it->second <= current) {
        return -2; // Expired
    }
    return static_cast<int64_t>(exp_it->second - current);
}

bool Database::is_expired(std::string_view key) {
    if (m_expires.empty()) return false;
    auto it = m_expires.find(key);
    if (it == m_expires.end()) return false;
    return it->second <= now_ms();
}

std::vector<std::string> Database::sample_keys(size_t count) {
    std::vector<std::string> sampled;
    if (m_dict.empty()) return sampled;

    size_t target_count = std::min(count, m_dict.size());
    sampled.reserve(target_count);
    static thread_local std::mt19937 gen{std::random_device{}()};
    size_t num_buckets = m_dict.bucket_count();
    std::uniform_int_distribution<size_t> bucket_dist(0, num_buckets - 1);

    size_t max_tries = count * 5;
    size_t tries = 0;
    while (sampled.size() < target_count && tries++ < max_tries) {
        size_t b = bucket_dist(gen);
        if (m_dict.bucket_size(b) > 0) {
            auto it = m_dict.begin(b);
            if (std::find(sampled.begin(), sampled.end(), it->first) == sampled.end()) {
                sampled.push_back(it->first);
            }
        }
    }

    if (sampled.size() < target_count) {
        for (auto it = m_dict.begin(); it != m_dict.end() && sampled.size() < target_count; ++it) {
            if (std::find(sampled.begin(), sampled.end(), it->first) == sampled.end()) {
                sampled.push_back(it->first);
            }
        }
    }
    return sampled;
}

std::vector<std::string> Database::sample_expires(size_t count) {
    std::vector<std::string> sampled;
    if (m_expires.empty()) return sampled;

    size_t target_count = std::min(count, m_expires.size());
    sampled.reserve(target_count);
    static thread_local std::mt19937 gen{std::random_device{}()};
    size_t num_buckets = m_expires.bucket_count();
    std::uniform_int_distribution<size_t> bucket_dist(0, num_buckets - 1);

    size_t max_tries = count * 5;
    size_t tries = 0;
    while (sampled.size() < target_count && tries++ < max_tries) {
        size_t b = bucket_dist(gen);
        if (m_expires.bucket_size(b) > 0) {
            auto it = m_expires.begin(b);
            if (std::find(sampled.begin(), sampled.end(), it->first) == sampled.end()) {
                sampled.push_back(it->first);
            }
        }
    }

    if (sampled.size() < target_count) {
        for (auto it = m_expires.begin(); it != m_expires.end() && sampled.size() < target_count; ++it) {
            if (std::find(sampled.begin(), sampled.end(), it->first) == sampled.end()) {
                sampled.push_back(it->first);
            }
        }
    }
    return sampled;
}

} // namespace rundb::core
