#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <utility>

namespace rundb::protocol {

/**
 * @brief High-level RESP protocol encoder and decoder.
 */
class RespParser {
public:
    static std::pair<std::vector<std::string>, size_t> parse_command(std::string_view buffer);

    // Serialization utilities
    static std::string encode_ok() { return "+OK\r\n"; }
    static std::string encode_pong() { return "+PONG\r\n"; }
    static std::string encode_nil() { return "$-1\r\n"; }
    static std::string encode_simple_string(std::string_view str);
    static std::string encode_error(std::string_view err);
    static std::string encode_integer(int64_t val);
    static std::string encode_bulk_string(std::string_view str);
    static std::string encode_array(const std::vector<std::string>& elements);
    static std::string encode_null_array() { return "*-1\r\n"; }
};

} // namespace rundb::protocol
