#include "protocol/resp_parser.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rundb {

    // =========================================================================
    // RESPValue Factory Constructors
    // =========================================================================

    RESPValue RESPValue::simple_string(std::string value) {
        RESPValue v;
        v.type_ = Type::SimpleString;
        v.data_.emplace<std::string>(std::move(value));
        return v;
    }

    RESPValue RESPValue::simple_string(std::string_view value) {
        RESPValue v;
        v.type_ = Type::SimpleString;
        v.data_.emplace<std::string>(value);
        return v;
    }

    RESPValue RESPValue::simple_string(const char* value) {
        RESPValue v;
        v.type_ = Type::SimpleString;
        v.data_.emplace<std::string>(value);
        return v;
    }

    RESPValue RESPValue::error(std::string value) {
        RESPValue v;
        v.type_ = Type::Error;
        v.data_.emplace<std::string>(std::move(value));
        return v;
    }

    RESPValue RESPValue::error(std::string_view value) {
        RESPValue v;
        v.type_ = Type::Error;
        v.data_.emplace<std::string>(value);
        return v;
    }

    RESPValue RESPValue::error(const char* value) {
        RESPValue v;
        v.type_ = Type::Error;
        v.data_.emplace<std::string>(value);
        return v;
    }

    RESPValue RESPValue::bulk_string(std::string value) {
        RESPValue v;
        v.type_ = Type::BulkString;
        v.data_.emplace<std::string>(std::move(value));
        return v;
    }

    RESPValue RESPValue::bulk_string(std::string_view value) {
        RESPValue v;
        v.type_ = Type::BulkString;
        v.data_.emplace<std::string>(value);
        return v;
    }

    RESPValue RESPValue::bulk_string(const char* value) {
        RESPValue v;
        v.type_ = Type::BulkString;
        v.data_.emplace<std::string>(value);
        return v;
    }

    RESPValue RESPValue::array(std::vector<RESPValue> value) {
        RESPValue v;
        v.type_ = Type::Array;
        v.data_.emplace<std::vector<RESPValue>>(std::move(value));
        return v;
    }

    RESPValue RESPValue::ok() {
        return simple_string("OK");
    }

    RESPValue RESPValue::pong() {
        return simple_string("PONG");
    }

    // =========================================================================
    // RESP Serialization
    // =========================================================================

    std::string RESPValue::serialize() const {
        switch (type_) {
            case Type::SimpleString:
                return "+" + string() + "\r\n";
            case Type::Error:
                return "-" + string() + "\r\n";
            case Type::Integer:
                return ":" + std::to_string(integer()) + "\r\n";
            case Type::BulkString: {
                const auto& str = string();
                return "$" + std::to_string(str.size()) + "\r\n" + str + "\r\n";
            }
            case Type::Array: {
                const auto& arr = array();
                std::string res;
                res.reserve(32 + arr.size() * 16);
                res += "*" + std::to_string(arr.size()) + "\r\n";
                for (const auto& item : arr) {
                    res += item.serialize();
                }
                return res;
            }
            case Type::Null:
            default:
                return "$-1\r\n";
        }
    }

    std::string RESPProcessor::serialize_simple_string(std::string_view value) {
        std::string res;
        res.reserve(1 + value.size() + 2);
        res.push_back('+');
        res.append(value);
        res.append("\r\n");
        return res;
    }

    std::string RESPProcessor::serialize_error(std::string_view value) {
        std::string res;
        res.reserve(1 + value.size() + 2);
        res.push_back('-');
        res.append(value);
        res.append("\r\n");
        return res;
    }

    std::string RESPProcessor::serialize_integer(std::int64_t value) {
        char buf[32];
        buf[0] = ':';
        auto [ptr, ec] = std::to_chars(buf + 1, buf + sizeof(buf) - 2, value);
        *ptr++ = '\r';
        *ptr++ = '\n';
        return std::string(buf, ptr - buf);
    }

    std::string RESPProcessor::serialize_bulk_string(std::string_view value) {
        char len_buf[32];
        len_buf[0] = '$';
        auto [ptr, ec] = std::to_chars(len_buf + 1, len_buf + sizeof(len_buf) - 2, value.size());
        *ptr++ = '\r';
        *ptr++ = '\n';

        std::string res;
        res.reserve((ptr - len_buf) + value.size() + 2);
        res.append(len_buf, ptr - len_buf);
        res.append(value);
        res.append("\r\n");
        return res;
    }

    std::string RESPProcessor::serialize_null() {
        return "$-1\r\n";
    }

    std::string RESPProcessor::serialize_array(const std::vector<std::string>& elements) {
        std::string res;
        res.reserve(16 + elements.size() * 32);
        res.push_back('*');
        res.append(std::to_string(elements.size()));
        res.append("\r\n");
        for (const auto& elem : elements) {
            res.append(serialize_bulk_string(elem));
        }
        return res;
    }

    // =========================================================================
    // RESP Parsing Implementation
    // =========================================================================

    /**
     * @brief Parses an unsigned decimal length prefix up to CRLF with overflow checking.
     *
     * Used by read_bulk_string and read_array to extract payload lengths.
     * Guaranteed not to overflow std::size_t.
     */
    bool RESPProcessor::read_length(
        std::string_view data,
        std::size_t& length,
        std::size_t& consumed
    ) {
        if (data.empty()) {
            return false;
        }

        std::size_t pos = 0;
        std::size_t val = 0;

        while (pos < data.size()) {
            char c = data[pos];
            if (c >= '0' && c <= '9') {
                if (__builtin_mul_overflow(val, 10, &val) ||
                    __builtin_add_overflow(val, static_cast<std::size_t>(c - '0'), &val)) {
                    return false; // Arithmetic overflow
                }
                ++pos;
            } else if (c == '\r') {
                if (pos == 0) {
                    return false; // Empty number before CRLF is invalid
                }
                if (pos + 1 < data.size() && data[pos + 1] == '\n') {
                    length = val;
                    consumed = pos + 2;
                    return true;
                }
                return false; // Incomplete CRLF
            } else {
                return false; // Non-digit character
            }
        }

        return false; // Incomplete line
    }

    /**
     * @brief Parses simple strings: "+<string>\r\n"
     */
    RESPParseResult RESPProcessor::read_simple_string(
        std::string_view data,
        RESPValue& value,
        std::size_t& consumed
    ) {
        std::size_t pos = data.find("\r\n", 1);
        if (pos == std::string_view::npos) {
            return {RESPParseStatus::Incomplete, 0};
        }

        value = RESPValue::simple_string(data.substr(1, pos - 1));
        consumed = pos + 2;

        return {RESPParseStatus::Complete, consumed};
    }

    /**
     * @brief Parses error messages: "-<error>\r\n"
     */
    RESPParseResult RESPProcessor::read_error(
        std::string_view data,
        RESPValue& value,
        std::size_t& consumed
    ) {
        std::size_t pos = data.find("\r\n", 1);
        if (pos == std::string_view::npos) {
            return {RESPParseStatus::Incomplete, 0};
        }

        value = RESPValue::error(data.substr(1, pos - 1));
        consumed = pos + 2;

        return {RESPParseStatus::Complete, consumed};
    }

    /**
     * @brief Parses signed 64-bit integer values: ":[<+|->]<number>\r\n"
     */
    RESPParseResult RESPProcessor::read_integer(
        std::string_view data,
        RESPValue& value,
        std::size_t& consumed
    ) {
        std::size_t pos = data.find("\r\n", 1);
        if (pos == std::string_view::npos) {
            return {RESPParseStatus::Incomplete, 0};
        }

        std::string_view number = data.substr(1, pos - 1);
        if (number.empty()) {
            return {RESPParseStatus::Error, "Invalid Integer"};
        }

        std::string_view parse_slice = number;
        if (parse_slice[0] == '+') {
            parse_slice.remove_prefix(1);
            if (parse_slice.empty()) {
                return {RESPParseStatus::Error, "Invalid Integer"};
            }
        }

        std::int64_t result = 0;
        auto [ptr, ec] = std::from_chars(parse_slice.data(), parse_slice.data() + parse_slice.size(), result);
        if (ec != std::errc{} || ptr != parse_slice.data() + parse_slice.size()) {
            return {RESPParseStatus::Error, "Invalid Integer"};
        }

        value = RESPValue::integer(result);
        consumed = pos + 2;

        return {RESPParseStatus::Complete, consumed};
    }

    /**
     * @brief Parses bulk strings: "$<length>\r\n<payload>\r\n" or "$-1\r\n" (null)
     */
    RESPParseResult RESPProcessor::read_bulk_string(
        std::string_view data,
        RESPValue& value,
        std::size_t& consumed
    ) {
        // Fast-path for null bulk string: "$-1\r\n"
        if (data.size() >= 2 && data[1] == '-') {
            if (data.size() >= 5) {
                if (data[2] == '1' && data[3] == '\r' && data[4] == '\n') {
                    value = RESPValue::null();
                    consumed = 5;
                    return {RESPParseStatus::Complete, 5};
                }
                return {RESPParseStatus::Error, "Invalid null bulk string"};
            }
            if (data.size() == 2 || (data.size() == 3 && data[2] == '1') || (data.size() == 4 && data[2] == '1' && data[3] == '\r')) {
                return {RESPParseStatus::Incomplete, 0};
            }
            return {RESPParseStatus::Error, "Invalid bulk string length"};
        }

        std::size_t length = 0;
        std::size_t length_consumed = 0;

        if (!read_length(data.substr(1), length, length_consumed)) {
            if (data.substr(1).find("\r\n") != std::string_view::npos) {
                return {RESPParseStatus::Error, "Invalid bulk string length"};
            }
            return {RESPParseStatus::Incomplete, 0};
        }

        std::size_t payload_start = 1 + length_consumed;

        // Prevent memory overflow calculation
        if (length > std::string_view::npos - payload_start - 2) {
            return {RESPParseStatus::Error, "Bulk string length overflow"};
        }

        // Check if full payload plus trailing CRLF is present
        if (data.size() < payload_start + length + 2) {
            return {RESPParseStatus::Incomplete, 0};
        }

        // Verify trailing CRLF
        if (data[payload_start + length] != '\r' || data[payload_start + length + 1] != '\n') {
            return {RESPParseStatus::Error, "Invalid bulk string termination"};
        }

        value = RESPValue::bulk_string(data.substr(payload_start, length));
        consumed = payload_start + length + 2;

        return {RESPParseStatus::Complete, consumed};
    }

    /**
     * @brief Parses RESP arrays: "*<count>\r\n<element 1>...<element N>" or "*-1\r\n" (null)
     */
    RESPParseResult RESPProcessor::read_array(
        std::string_view data,
        RESPValue& value,
        std::size_t& consumed
    ) {
        // Fast-path for null array: "*-1\r\n"
        if (data.size() >= 2 && data[1] == '-') {
            if (data.size() >= 5) {
                if (data[2] == '1' && data[3] == '\r' && data[4] == '\n') {
                    value = RESPValue::null();
                    consumed = 5;
                    return {RESPParseStatus::Complete, 5};
                }
                return {RESPParseStatus::Error, "Invalid null array"};
            }
            if (data.size() == 2 || (data.size() == 3 && data[2] == '1') || (data.size() == 4 && data[2] == '1' && data[3] == '\r')) {
                return {RESPParseStatus::Incomplete, 0};
            }
            return {RESPParseStatus::Error, "Invalid array length"};
        }

        std::size_t length = 0;
        std::size_t length_consumed = 0;

        if (!read_length(data.substr(1), length, length_consumed)) {
            if (data.substr(1).find("\r\n") != std::string_view::npos) {
                return {RESPParseStatus::Error, "Invalid array length"};
            }
            return {RESPParseStatus::Incomplete, 0};
        }

        if (length == 0) {
            value = RESPValue::array(std::vector<RESPValue>{});
            consumed = 1 + length_consumed;
            return {RESPParseStatus::Complete, consumed};
        }

        std::size_t position = 1 + length_consumed;
        std::vector<RESPValue> elements;
        // Cap initial reservation to prevent OOM DOS attacks on untrusted input
        std::size_t initial_reserve = std::min(length, (data.size() - position) / 3 + 1);
        elements.reserve(initial_reserve);

        for (std::size_t i = 0; i < length; ++i) {
            if (position >= data.size()) {
                return {RESPParseStatus::Incomplete, 0};
            }

            RESPValue element;
            std::size_t element_consumed = 0;

            auto result = decode_one(data.substr(position), element, element_consumed);
            if (result.status != RESPParseStatus::Complete) {
                return result;
            }

            elements.push_back(std::move(element));
            position += element_consumed;
        }

        value = RESPValue::array(std::move(elements));
        consumed = position;

        return {RESPParseStatus::Complete, consumed};
    }

    /**
     * @brief Parses plain-text inline commands (e.g. "PING\r\n" or "SET foo bar\r\n").
     *
     * Enables compatibility with telnet, netcat, health-check probes, and simple scripts.
     * Splits arguments by whitespace while respecting single/double quoted strings.
     */
    RESPParseResult RESPProcessor::read_inline_command(
        std::string_view data,
        RESPValue& value,
        std::size_t& consumed
    ) {
        std::size_t newline = data.find('\n');
        if (newline == std::string_view::npos) {
            return {RESPParseStatus::Incomplete, 0};
        }

        std::size_t line_end = newline;
        if (line_end > 0 && data[line_end - 1] == '\r') {
            --line_end;
        }

        std::string_view line = data.substr(0, line_end);
        consumed = newline + 1; // Consume through \n

        // Tokenize line into bulk string elements
        std::vector<RESPValue> elements;
        std::size_t pos = 0;

        while (pos < line.size()) {
            // Skip leading whitespace
            while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) {
                ++pos;
            }
            if (pos >= line.size()) {
                break;
            }

            std::size_t start = pos;
            if (line[pos] == '"' || line[pos] == '\'') {
                char quote = line[pos++];
                start = pos;
                while (pos < line.size() && line[pos] != quote) {
                    if (line[pos] == '\\' && pos + 1 < line.size()) {
                        pos += 2; // Skip escaped character
                    } else {
                        ++pos;
                    }
                }
                std::string token(line.substr(start, pos - start));
                if (pos < line.size() && line[pos] == quote) {
                    ++pos;
                }
                elements.push_back(RESPValue::bulk_string(std::move(token)));
            } else {
                while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t') {
                    ++pos;
                }
                elements.push_back(RESPValue::bulk_string(line.substr(start, pos - start)));
            }
        }

        value = RESPValue::array(std::move(elements));
        return {RESPParseStatus::Complete, consumed};
    }

    /**
     * @brief Dispatches parsing of a single RESP item based on its leading type indicator.
     */
    RESPParseResult RESPProcessor::decode_one(
        std::string_view data,
        RESPValue& value,
        std::size_t& consumed
    ) {
        if (data.empty()) {
            return {RESPParseStatus::Incomplete, 0};
        }

        switch (data[0]) {
            case '+':
                return read_simple_string(data, value, consumed);
            case '-':
                return read_error(data, value, consumed);
            case ':':
                return read_integer(data, value, consumed);
            case '$':
                return read_bulk_string(data, value, consumed);
            case '*':
                return read_array(data, value, consumed);
            default:
                // Fallback to plain-text inline command parsing (telnet/netcat)
                return read_inline_command(data, value, consumed);
        }
    }

    /**
     * @brief Decodes a stream of bytes into one or more complete RESP values.
     * Supports pipelined command streams.
     */
    RESPParseResult RESPProcessor::decode(std::string_view data) {
        if (data.empty()) {
            return {RESPParseStatus::Incomplete, 0};
        }

        std::vector<RESPValue> values;
        values.reserve(4);

        std::size_t idx = 0;
        while (idx < data.size()) {
            RESPValue value;
            std::size_t consumed = 0;

            auto result = decode_one(data.substr(idx), value, consumed);

            if (result.status == RESPParseStatus::Error) {
                return {
                    RESPParseStatus::Error,
                    std::move(values),
                    idx,
                    std::move(result.error)
                };
            }

            if (result.status == RESPParseStatus::Incomplete) {
                return {
                    RESPParseStatus::Incomplete,
                    std::move(values),
                    idx,
                    {}
                };
            }

            // Only push non-empty commands (empty inline commands like standalone newlines are skipped)
            if (value.type() != RESPValue::Type::Array || !value.array().empty()) {
                values.push_back(std::move(value));
            }
            idx += consumed;
        }

        return {
            RESPParseStatus::Complete,
            std::move(values),
            idx,
            {}
        };
    }

    /**
     * @brief Converts a parsed RESPValue into a normalized Cmd struct.
     */
    Cmd RESPProcessor::to_cmd(const RESPValue& value) {
        Cmd cmd;
        if (value.type() == RESPValue::Type::Array) {
            const auto& elements = value.array();
            if (!elements.empty()) {
                cmd.command = elements[0].string();
                // Normalize command name to uppercase for fast branchless matching
                for (char& c : cmd.command) {
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                }

                cmd.args.reserve(elements.size() - 1);
                for (std::size_t i = 1; i < elements.size(); ++i) {
                    cmd.args.push_back(elements[i].string());
                }
            }
        } else if (value.type() == RESPValue::Type::BulkString || value.type() == RESPValue::Type::SimpleString) {
            cmd.command = value.string();
            for (char& c : cmd.command) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
        }
        return cmd;
    }

} // namespace rundb