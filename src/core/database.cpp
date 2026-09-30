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

bool Database::is_expired(std::string_view key) {
    if (m_expires.empty()) return false;
    auto it = m_expires.find(key);
    if (it == m_expires.end()) return false;
    return it->second <= now_ms();
}

std::vector<std::string> Database::sample_keys(size_t count) {
    std::vector<std::string> sampled;
    if (m_dict.empty()) return sampled;

    sampled.reserve(std::min(count, m_dict.size()));
    static thread_local std::mt19937 gen{std::random_device{}()};
    std::uniform_int_distribution<size_t> dist(0, m_dict.size() - 1);

    for (size_t i = 0; i < count && !m_dict.empty(); ++i) {
        size_t step = dist(gen) % m_dict.size();
        auto cur = m_dict.begin();
        std::advance(cur, step);
        sampled.push_back(cur->first);
    }
    return sampled;
}

std::vector<std::string> Database::sample_expires(size_t count) {
    std::vector<std::string> sampled;
    if (m_expires.empty()) return sampled;

    sampled.reserve(std::min(count, m_expires.size()));
    static thread_local std::mt19937 gen{std::random_device{}()};
    std::uniform_int_distribution<size_t> dist(0, m_expires.size() - 1);

    for (size_t i = 0; i < count && !m_expires.empty(); ++i) {
        size_t step = dist(gen) % m_expires.size();
        auto cur = m_expires.begin();
        std::advance(cur, step);
        sampled.push_back(cur->first);
    }
    return sampled;
}

} // namespace rundb::core
