#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <string>
#include <vector>
#include <variant>
#include <utility>
#include "protocol/cmd.hpp"

namespace rundb {

    /**
     * @brief Represents a parsed RESP (REdis Serialization Protocol) data entity.
     *
     * RESP supports five primary data types:
     * - Simple Strings: starts with '+' (e.g., "+OK\r\n")
     * - Errors: starts with '-' (e.g., "-ERR unknown command\r\n")
     * - Integers: starts with ':' (e.g., ":1000\r\n")
     * - Bulk Strings: starts with '$' (binary-safe strings, e.g., "$6\r\nfoobar\r\n" or "$-1\r\n" for null)
     * - Arrays: starts with '*' (ordered collection of RESP elements, e.g., "*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n")
     */
    class RESPValue {
        public:
            enum class Type : std::uint8_t {
                SimpleString,
                Error,
                Integer,
                BulkString,
                Array,
                Null
            };

        private:
            Type type_{Type::Null};
            std::variant<std::monostate, std::int64_t, std::string, std::vector<RESPValue>> data_{};

        public:
            RESPValue() noexcept = default;
            ~RESPValue() = default;
            RESPValue(const RESPValue&) = default;
            RESPValue& operator=(const RESPValue&) = default;
            RESPValue(RESPValue&&) noexcept = default;
            RESPValue& operator=(RESPValue&&) noexcept = default;

            // Factory constructors
            static RESPValue simple_string(std::string value);
            static RESPValue simple_string(std::string_view value);
            static RESPValue simple_string(const char* value);

            static RESPValue error(std::string value);
            static RESPValue error(std::string_view value);
            static RESPValue error(const char* value);

            static RESPValue integer(std::int64_t value) noexcept;

            static RESPValue bulk_string(std::string value);
            static RESPValue bulk_string(std::string_view value);
            static RESPValue bulk_string(const char* value);

            static RESPValue array(std::vector<RESPValue> value);
            static RESPValue null() noexcept;

            // Common constants
            static RESPValue ok();
            static RESPValue pong();

            [[nodiscard]] Type type() const noexcept { return type_; }

            /**
             * @brief Returns the underlying string for SimpleString, Error, or BulkString.
             * Returns an empty string reference if the variant does not hold a string.
             */
            [[nodiscard]] const std::string& string() const noexcept;

            /**
             * @brief Returns the underlying 64-bit integer for Integer types, or 0.
             */
            [[nodiscard]] std::int64_t integer() const noexcept;

            /**
             * @brief Returns the underlying array elements for Array types.
             * Returns an empty vector reference if the variant does not hold an array.
             */
            [[nodiscard]] const std::vector<RESPValue>& array() const noexcept;

            /**
             * @brief Serializes the RESPValue into standard RESP wire format.
             */
            [[nodiscard]] std::string serialize() const;
    };

    inline RESPValue RESPValue::integer(std::int64_t value) noexcept {
        RESPValue v;
        v.type_ = Type::Integer;
        v.data_.emplace<std::int64_t>(value);
        return v;
    }

    inline RESPValue RESPValue::null() noexcept {
        return RESPValue{};
    }

    inline const std::string& RESPValue::string() const noexcept {
        static const std::string empty_string;
        if (const auto* s = std::get_if<std::string>(&data_)) {
            return *s;
        }
        return empty_string;
    }

    inline std::int64_t RESPValue::integer() const noexcept {
        if (const auto* i = std::get_if<std::int64_t>(&data_)) {
            return *i;
        }
        return 0;
    }

    inline const std::vector<RESPValue>& RESPValue::array() const noexcept {
        static const std::vector<RESPValue> empty_array;
        if (const auto* a = std::get_if<std::vector<RESPValue>>(&data_)) {
            return *a;
        }
        return empty_array;
    }

    /**
     * @brief Indicates the status of a RESP parsing operation.
     */
    enum class RESPParseStatus : std::uint8_t {
        Complete,    ///< The data contained one or more fully parsed RESP values.
        Incomplete,  ///< The buffer contains a partial message; more bytes needed from socket.
        Error        ///< Protocol syntax error encountered in the byte stream.
    };

    /**
     * @brief Encapsulates the outcome of decoding a buffer of RESP data.
     */
    struct RESPParseResult {
        RESPParseStatus status{RESPParseStatus::Incomplete};
        std::vector<RESPValue> values{};  ///< Successfully parsed values (supports pipelined batches)
        std::size_t consumed{0};           ///< Number of bytes consumed from input buffer
        std::string error{};              ///< Error message if status == Error

        RESPParseResult() noexcept = default;

        RESPParseResult(RESPParseStatus s, std::size_t c = 0) noexcept
            : status(s), consumed(c) {}

        RESPParseResult(RESPParseStatus s, std::string err)
            : status(s), error(std::move(err)) {}

        RESPParseResult(RESPParseStatus s, std::vector<RESPValue> v, std::size_t c, std::string err = {}) noexcept
            : status(s), values(std::move(v)), consumed(c), error(std::move(err)) {}
    };

    /**
     * @brief High-performance decoder and encoder for the Redis Serialization Protocol (RESP).
     *
     * Provides zero-copy parsing where feasible using std::string_view, length overflow protection,
     * support for both standard RESP arrays and plain-text inline commands (telnet/netcat friendly),
     * and direct fast-path wire format serialization.
     */
    class RESPProcessor {
        public:
            /**
             * @brief Decodes all complete RESP values available in the data view.
             * @param data Byte slice containing raw data from client socket.
             * @return RESPParseResult with parsed values and number of consumed bytes.
             */
            static RESPParseResult decode(std::string_view data);

            /**
             * @brief Converts a parsed RESPValue (typically an Array of BulkStrings) to a Cmd struct.
             * Normalizes the command name to uppercase for O(1) matching.
             */
            static Cmd to_cmd(const RESPValue& value);

            // Fast wire-format serialization helpers (avoids intermediate tree allocation)
            static std::string serialize_simple_string(std::string_view value);
            static std::string serialize_error(std::string_view value);
            static std::string serialize_integer(std::int64_t value);
            static std::string serialize_bulk_string(std::string_view value);
            static std::string serialize_null();
            static std::string serialize_array(const std::vector<std::string>& elements);

            // Pre-computed common RESP wire tokens
            static constexpr std::string_view RESP_OK = "+OK\r\n";
            static constexpr std::string_view RESP_PONG = "+PONG\r\n";
            static constexpr std::string_view RESP_NIL = "$-1\r\n";
            static constexpr std::string_view RESP_ZERO = ":0\r\n";
            static constexpr std::string_view RESP_ONE = ":1\r\n";

            static RESPParseResult decode_one(std::string_view data, RESPValue& value, std::size_t& consumed);

        private:
            static RESPParseResult read_simple_string(std::string_view data, RESPValue& value, std::size_t& consumed);
            static RESPParseResult read_error(std::string_view data, RESPValue& value, std::size_t& consumed);
            static RESPParseResult read_integer(std::string_view data, RESPValue& value, std::size_t& consumed);
            static RESPParseResult read_bulk_string(std::string_view data, RESPValue& value, std::size_t& consumed);
            static RESPParseResult read_array(std::string_view data, RESPValue& value, std::size_t& consumed);
            static RESPParseResult read_inline_command(std::string_view data, RESPValue& value, std::size_t& consumed);
            static bool read_length(std::string_view data, std::size_t& length, std::size_t& consumed);
    };

} // namespace rundb