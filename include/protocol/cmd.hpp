#pragma once

#include <string>
#include <vector>

namespace rundb {

    /**
     * @brief Represents a parsed database command and its associated arguments.
     *
     * In the RESP (REdis Serialization Protocol) model, client commands are submitted
     * as arrays of bulk strings. The first element is the command name (e.g., "SET", "GET"),
     * and the remaining elements are positional arguments.
     *
     * For maximum performance during command dispatch, command names are normalized
     * to uppercase upon parsing.
     */
    struct Cmd {
        std::string command;            ///< Normalized uppercase command name (e.g., "PING", "SET", "GET")
        std::vector<std::string> args;  ///< Ordered list of command argument strings

        Cmd() = default;

        /**
         * @brief Construct a command with a name and optional arguments.
         * @param cmd Command name.
         * @param a Positional arguments.
         */
        Cmd(std::string cmd, std::vector<std::string> a = {})
            : command(std::move(cmd)), args(std::move(a)) {}

        /**
         * @brief Check whether the command has no name specified.
         */
        [[nodiscard]] bool empty() const noexcept {
            return command.empty();
        }

        /**
         * @brief Get the count of arguments provided to the command.
         */
        [[nodiscard]] std::size_t argc() const noexcept {
            return args.size();
        }
    };

} // namespace rundb