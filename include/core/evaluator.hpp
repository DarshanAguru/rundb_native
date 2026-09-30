#pragma once

#include <string>
#include <vector>
#include "core/store.hpp"
#include "core/client_context.hpp"

namespace rundb::core {

/**
 * @brief High-performance command evaluator and dispatcher for RunDB.
 *
 * Implements wire-compatible RESP command logic for Strings, Lists, Sets,
 * Partitions, Expirations, Transactions, and Admin controls.
 */
class Evaluator {
public:
    static std::string evaluate(Store& store, ClientContext& ctx, const std::vector<std::string>& tokens);

private:
    // Category evaluators
    static std::string eval_strings(Store& store, Database& db, const std::string& cmd, const std::vector<std::string>& tokens);
    static std::string eval_lists(Store& store, Database& db, const std::string& cmd, const std::vector<std::string>& tokens);
    static std::string eval_sets(Store& store, Database& db, const std::string& cmd, const std::vector<std::string>& tokens);
    static std::string eval_generic(Store& store, Database& db, const std::string& cmd, const std::vector<std::string>& tokens);
    static std::string eval_admin(Store& store, ClientContext& ctx, Database& db, const std::string& cmd, const std::vector<std::string>& tokens);
    static std::string eval_transaction(Store& store, ClientContext& ctx, const std::string& cmd, const std::vector<std::string>& tokens);
};

} // namespace rundb::core
