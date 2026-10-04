#pragma once

#include <vector>
#include <string>

namespace rundb::core {

/**
 * @brief State associated with a connected client session.
 */
struct ClientContext {
    uint64_t id{1};
    int active_db{0};
    bool in_transaction{false};
    std::vector<std::vector<std::string>> tx_queue;
    std::string name;
    std::string addr{"127.0.0.1:0"};
    std::string laddr{"127.0.0.1:7379"};
    int fd{-1};
    uint64_t created_time_s{0};
    uint64_t last_interaction_s{0};
    std::string lib_name;
    std::string lib_ver;
    std::string last_cmd{"client"};
    bool close_requested{false};

    void reset_transaction() noexcept {
        in_transaction = false;
        tx_queue.clear();
    }
};

} // namespace rundb::core
