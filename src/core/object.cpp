#include "core/object.hpp"
#include <chrono>
#include <charconv>
#include <atomic>
#include <cctype>

namespace rundb::core {

static constexpr size_t SET_MAX_INTSET_ENTRIES = 512;
static std::atomic<uint32_t> s_lru_clock{0};

void RunDBObject::update_global_lru_clock() noexcept {
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
    s_lru_clock.store(static_cast<uint32_t>(now & 0x00FFFFFF), std::memory_order_relaxed);
}

uint32_t RunDBObject::get_lru_clock() noexcept {
    uint32_t clock = s_lru_clock.load(std::memory_order_relaxed);
    if (__builtin_expect(clock == 0, 0)) {
        update_global_lru_clock();
        clock = s_lru_clock.load(std::memory_order_relaxed);
    }
    return clock;
}

RunDBObject::RunDBObject(ObjectType type, ObjectEncoding enc, ValueVariant val)
    : m_val(std::move(val)) {
    uint32_t t = static_cast<uint32_t>(type) & 0x0F;
    uint32_t e = (static_cast<uint32_t>(enc) & 0x0F) << 4;
    uint32_t clock = (get_lru_clock() & 0x00FFFFFF) << 8;
    m_header = t | e | clock;
}

void RunDBObject::update_lru() noexcept {
    uint32_t clock = (get_lru_clock() & 0x00FFFFFF) << 8;
    m_header = (m_header & 0x000000FF) | clock;
}

std::shared_ptr<RunDBObject> RunDBObject::create_string(std::string_view val) {
    if (!val.empty() && (std::isdigit(static_cast<unsigned char>(val[0])) || val[0] == '-' || val[0] == '+')) {
        int64_t parsed_int = 0;
        auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), parsed_int);
        if (ec == std::errc() && ptr == val.data() + val.size()) {
            return create_int(parsed_int);
        }
    }
    return std::make_shared<RunDBObject>(
        ObjectType::String,
        ObjectEncoding::Raw,
        internals::SDS(val)
    );
}

std::shared_ptr<RunDBObject> RunDBObject::create_int(int64_t val) {
    return std::make_shared<RunDBObject>(
        ObjectType::String,
        ObjectEncoding::InlineInt,
        val
    );
}

std::shared_ptr<RunDBObject> RunDBObject::create_list() {
    return std::make_shared<RunDBObject>(
        ObjectType::List,
        ObjectEncoding::QuickList,
        internals::QuickList()
    );
}

std::shared_ptr<RunDBObject> RunDBObject::create_set() {
    return std::make_shared<RunDBObject>(
        ObjectType::Set,
        ObjectEncoding::IntSet,
        internals::IntSet()
    );
}

std::string RunDBObject::get_string_value() const {
    if (encoding() == ObjectEncoding::InlineInt) {
        return std::to_string(std::get<int64_t>(m_val));
    }
    if (std::holds_alternative<StringData>(m_val)) {
        return std::get<StringData>(m_val).to_string();
    }
    return "";
}

std::string_view RunDBObject::get_string_view() const noexcept {
    if (std::holds_alternative<StringData>(m_val)) {
        return std::get<StringData>(m_val).view();
    }
    return {};
}

const internals::SDS* RunDBObject::get_sds() const noexcept {
    if (std::holds_alternative<StringData>(m_val)) {
        return &std::get<StringData>(m_val);
    }
    return nullptr;
}

internals::SDS* RunDBObject::get_sds() noexcept {
    if (std::holds_alternative<StringData>(m_val)) {
        return &std::get<StringData>(m_val);
    }
    return nullptr;
}

int64_t RunDBObject::get_int_value() const {
    if (encoding() == ObjectEncoding::InlineInt) {
        return std::get<int64_t>(m_val);
    }
    if (std::holds_alternative<StringData>(m_val)) {
        const auto& s = std::get<StringData>(m_val);
        int64_t val = 0;
        auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
        if (ec == std::errc() && ptr == s.data() + s.size()) {
            return val;
        }
    }
    return 0;
}

RunDBObject::ListData* RunDBObject::get_list() noexcept {
    if (std::holds_alternative<ListData>(m_val)) {
        return &std::get<ListData>(m_val);
    }
    return nullptr;
}

const RunDBObject::ListData* RunDBObject::get_list() const noexcept {
    if (std::holds_alternative<ListData>(m_val)) {
        return &std::get<ListData>(m_val);
    }
    return nullptr;
}

RunDBObject::IntSetData* RunDBObject::get_intset() noexcept {
    if (std::holds_alternative<IntSetData>(m_val)) {
        return &std::get<IntSetData>(m_val);
    }
    return nullptr;
}

const RunDBObject::IntSetData* RunDBObject::get_intset() const noexcept {
    if (std::holds_alternative<IntSetData>(m_val)) {
        return &std::get<IntSetData>(m_val);
    }
    return nullptr;
}

RunDBObject::HashSetData* RunDBObject::get_hashset() noexcept {
    if (std::holds_alternative<HashSetData>(m_val)) {
        return &std::get<HashSetData>(m_val);
    }
    return nullptr;
}

const RunDBObject::HashSetData* RunDBObject::get_hashset() const noexcept {
    if (std::holds_alternative<HashSetData>(m_val)) {
        return &std::get<HashSetData>(m_val);
    }
    return nullptr;
}

void RunDBObject::promote_to_hashset() {
    if (encoding() == ObjectEncoding::HashSet) return;

    HashSetData hs;
    if (std::holds_alternative<IntSetData>(m_val)) {
        const auto& is = std::get<IntSetData>(m_val);
        for (int64_t v : is.to_vector()) {
            hs.emplace(std::to_string(v));
        }
    }
    m_val = std::move(hs);
    uint32_t enc = (static_cast<uint32_t>(ObjectEncoding::HashSet) & 0x0F) << 4;
    m_header = (m_header & 0xFFFFFF0F) | enc;
}

bool RunDBObject::set_add(std::string_view member) {
    if (encoding() == ObjectEncoding::IntSet) {
        int64_t parsed_int = 0;
        auto [ptr, ec] = std::from_chars(member.data(), member.data() + member.size(), parsed_int);
        if (ec == std::errc() && ptr == member.data() + member.size()) {
            auto* is = get_intset();
            if (is->size() >= SET_MAX_INTSET_ENTRIES) {
                promote_to_hashset();
                return get_hashset()->emplace(member).second;
            }
            return is->add(parsed_int);
        } else {
            promote_to_hashset();
            return get_hashset()->emplace(member).second;
        }
    }

    if (auto* hs = get_hashset()) {
        return hs->emplace(member).second;
    }
    return false;
}

bool RunDBObject::set_remove(std::string_view member) {
    if (encoding() == ObjectEncoding::IntSet) {
        int64_t parsed_int = 0;
        auto [ptr, ec] = std::from_chars(member.data(), member.data() + member.size(), parsed_int);
        if (ec == std::errc() && ptr == member.data() + member.size()) {
            return get_intset()->remove(parsed_int);
        }
        return false;
    }
    if (auto* hs = get_hashset()) {
        auto it = hs->find(internals::SDS(member));
        if (it != hs->end()) {
            hs->erase(it);
            return true;
        }
        return false;
    }
    return false;
}

bool RunDBObject::set_contains(std::string_view member) const {
    if (encoding() == ObjectEncoding::IntSet) {
        int64_t parsed_int = 0;
        auto [ptr, ec] = std::from_chars(member.data(), member.data() + member.size(), parsed_int);
        if (ec == std::errc() && ptr == member.data() + member.size()) {
            return get_intset()->contains(parsed_int);
        }
        return false;
    }
    if (const auto* hs = get_hashset()) {
        return hs->find(internals::SDS(member)) != hs->end();
    }
    return false;
}

size_t RunDBObject::set_size() const {
    if (encoding() == ObjectEncoding::IntSet) {
        return get_intset()->size();
    }
    if (const auto* hs = get_hashset()) {
        return hs->size();
    }
    return 0;
}

size_t RunDBObject::memory_bytes() const noexcept {
    size_t base = sizeof(RunDBObject);
    if (encoding() == ObjectEncoding::Raw && std::holds_alternative<StringData>(m_val)) {
        const auto& s = std::get<StringData>(m_val);
        if (!s.is_sso()) {
            base += s.capacity() + 8 + 1;
        }
    } else if (encoding() == ObjectEncoding::QuickList && std::holds_alternative<ListData>(m_val)) {
        base += std::get<ListData>(m_val).memory_bytes();
    } else if (encoding() == ObjectEncoding::IntSet && std::holds_alternative<IntSetData>(m_val)) {
        base += std::get<IntSetData>(m_val).memory_bytes();
    } else if (encoding() == ObjectEncoding::HashSet && std::holds_alternative<HashSetData>(m_val)) {
        const auto& hs = std::get<HashSetData>(m_val);
        base += hs.bucket_count() * sizeof(void*);
        for (const auto& s : hs) {
            base += sizeof(void*) * 2 + (s.is_sso() ? 0 : s.capacity() + 8 + 1);
        }
    }
    return base;
}

} // namespace rundb::core
