#pragma once

#include <vector>
#include <string>

namespace rundb::core {

/**
 * @brief State associated with a connected client session.
 */
struct ClientContext {
    int active_db{0};
    bool in_transaction{false};
    std::vector<std::vector<std::string>> tx_queue;

    void reset_transaction() noexcept {
        in_transaction = false;
        tx_queue.clear();
    }
};

} // namespace rundb::core
