#include "core/internals/quicklist.hpp"
#include "core/internals/run_alloc.hpp"
#include <algorithm>

namespace rundb::core::internals {

void* QuickList::Node::operator new(size_t size) {
    return run_alloc::run_malloc(size);
}

void QuickList::Node::operator delete(void* ptr) noexcept {
    run_alloc::run_free(ptr);
}

size_t QuickList::Node::memory_bytes() const noexcept {
    size_t bytes = sizeof(Node);
    for (size_t i = 0; i < count; ++i) {
        bytes += items[start_idx + i].capacity();
    }
    return bytes;
}

QuickList::QuickList() = default;

QuickList::~QuickList() {
    clear();
}

void QuickList::clear() noexcept {
    Node* cur = m_head;
    while (cur) {
        Node* next = cur->next;
        delete cur;
        cur = next;
    }
    m_head = nullptr;
    m_tail = nullptr;
    m_total_elements = 0;
    m_node_count = 0;
}

QuickList::QuickList(const QuickList& other) {
    Node* cur = other.m_head;
    while (cur) {
        for (size_t i = 0; i < cur->count; ++i) {
            push_back(cur->items[cur->start_idx + i]);
        }
        cur = cur->next;
    }
}

QuickList::QuickList(QuickList&& other) noexcept
    : m_head(other.m_head), m_tail(other.m_tail),
      m_total_elements(other.m_total_elements), m_node_count(other.m_node_count) {
    other.m_head = nullptr;
    other.m_tail = nullptr;
    other.m_total_elements = 0;
    other.m_node_count = 0;
}

QuickList& QuickList::operator=(const QuickList& other) {
    if (this == &other) return *this;
    clear();
    Node* cur = other.m_head;
    while (cur) {
        for (size_t i = 0; i < cur->count; ++i) {
            push_back(cur->items[cur->start_idx + i]);
        }
        cur = cur->next;
    }
    return *this;
}

QuickList& QuickList::operator=(QuickList&& other) noexcept {
    if (this == &other) return *this;
    clear();
    m_head = other.m_head;
    m_tail = other.m_tail;
    m_total_elements = other.m_total_elements;
    m_node_count = other.m_node_count;
    other.m_head = nullptr;
    other.m_tail = nullptr;
    other.m_total_elements = 0;
    other.m_node_count = 0;
    return *this;
}

void QuickList::push_front(std::string_view val) {
    if (!m_head || m_head->start_idx == 0) {
        auto* node = new Node();
        node->start_idx = CHUNK_CAPACITY - 1;
        node->items[node->start_idx] = std::string(val);
        node->count = 1;
        node->next = m_head;
        if (m_head) {
            m_head->prev = node;
        } else {
            m_tail = node;
        }
        m_head = node;
        m_node_count++;
    } else {
        m_head->start_idx--;
        m_head->items[m_head->start_idx] = std::string(val);
        m_head->count++;
    }
    m_total_elements++;
}

void QuickList::push_back(std::string_view val) {
    if (!m_tail || m_tail->is_full()) {
        auto* node = new Node();
        node->start_idx = 0;
        node->items[0] = std::string(val);
        node->count = 1;
        node->prev = m_tail;
        if (m_tail) {
            m_tail->next = node;
        } else {
            m_head = node;
        }
        m_tail = node;
        m_node_count++;
    } else {
        size_t next_idx = m_tail->start_idx + m_tail->count;
        m_tail->items[next_idx] = std::string(val);
        m_tail->count++;
    }
    m_total_elements++;
}

std::optional<std::string> QuickList::pop_front() {
    if (!m_head || m_total_elements == 0) return std::nullopt;

    std::string val = std::move(m_head->items[m_head->start_idx]);
    m_head->start_idx++;
    m_head->count--;
    m_total_elements--;

    if (m_head->count == 0) {
        Node* next = m_head->next;
        delete m_head;
        m_head = next;
        if (m_head) {
            m_head->prev = nullptr;
        } else {
            m_tail = nullptr;
        }
        m_node_count--;
    }

    return val;
}

std::optional<std::string> QuickList::pop_back() {
    if (!m_tail || m_total_elements == 0) return std::nullopt;

    size_t last_idx = m_tail->start_idx + m_tail->count - 1;
    std::string val = std::move(m_tail->items[last_idx]);
    m_tail->count--;
    m_total_elements--;

    if (m_tail->count == 0) {
        Node* prev = m_tail->prev;
        delete m_tail;
        m_tail = prev;
        if (m_tail) {
            m_tail->next = nullptr;
        } else {
            m_head = nullptr;
        }
        m_node_count--;
    }

    return val;
}

std::optional<std::string> QuickList::at(int64_t index) const {
    if (m_total_elements == 0) return std::nullopt;

    // Support negative indexing (Redis style: -1 is last element)
    if (index < 0) {
        index = static_cast<int64_t>(m_total_elements) + index;
    }
    if (index < 0 || static_cast<size_t>(index) >= m_total_elements) {
        return std::nullopt;
    }

    auto target = static_cast<size_t>(index);

    // Fast-path: Walk from head or tail depending on proximity
    if (target < (m_total_elements >> 1)) {
        Node* cur = m_head;
        size_t accumulated = 0;
        while (cur) {
            if (accumulated + cur->count > target) {
                size_t offset = target - accumulated;
                return cur->items[cur->start_idx + offset];
            }
            accumulated += cur->count;
            cur = cur->next;
        }
    } else {
        Node* cur = m_tail;
        size_t accumulated = m_total_elements;
        while (cur) {
            accumulated -= cur->count;
            if (target >= accumulated) {
                size_t offset = target - accumulated;
                return cur->items[cur->start_idx + offset];
            }
            cur = cur->prev;
        }
    }

    return std::nullopt;
}

std::vector<std::string> QuickList::range(int64_t start, int64_t stop) const {
    std::vector<std::string> result;
    if (m_total_elements == 0) return result;

    if (start < 0) start = static_cast<int64_t>(m_total_elements) + start;
    if (stop < 0) stop = static_cast<int64_t>(m_total_elements) + stop;

    if (start < 0) start = 0;
    if (static_cast<size_t>(start) >= m_total_elements || start > stop) {
        return result;
    }
    if (static_cast<size_t>(stop) >= m_total_elements) {
        stop = static_cast<int64_t>(m_total_elements) - 1;
    }

    size_t u_start = static_cast<size_t>(start);
    size_t u_stop = static_cast<size_t>(stop);
    result.reserve(u_stop - u_start + 1);

    Node* cur = m_head;
    size_t accumulated = 0;
    while (cur && accumulated <= u_stop) {
        size_t node_end = accumulated + cur->count;
        if (node_end > u_start) {
            size_t seg_start = (u_start > accumulated) ? (u_start - accumulated) : 0;
            size_t seg_end = (u_stop < node_end - 1) ? (u_stop - accumulated) : (cur->count - 1);
            for (size_t i = seg_start; i <= seg_end; ++i) {
                result.push_back(cur->items[cur->start_idx + i]);
            }
        }
        accumulated += cur->count;
        cur = cur->next;
    }

    return result;
}

size_t QuickList::memory_bytes() const noexcept {
    size_t total = sizeof(QuickList);
    Node* cur = m_head;
    while (cur) {
        total += cur->memory_bytes();
        cur = cur->next;
    }
    return total;
}

} // namespace rundb::core::internals
