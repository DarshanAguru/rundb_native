#include "protocol/resp.hpp"
#include "protocol/resp_parser.hpp"

namespace rundb::protocol {

std::pair<std::vector<std::string>, size_t> RespParser::parse_command(std::string_view buffer) {
    if (buffer.empty()) return {{}, 0};

    // Fast-path for standard RESP Arrays of Bulk Strings: *<num>\r\n$<len>\r\n<payload>\r\n...
    if (buffer[0] == '*') {
        const char* ptr = buffer.data() + 1;
        const char* end = buffer.data() + buffer.size();

        // Parse number of elements
        size_t count = 0;
        const char* p = ptr;
        while (p < end && *p >= '0' && *p <= '9') {
            count = count * 10 + static_cast<size_t>(*p - '0');
            ++p;
        }
        if (p > ptr && p + 1 < end && *p == '\r' && *(p + 1) == '\n') {
            p += 2; // skip \r\n
            std::vector<std::string> tokens;
            tokens.reserve(count);
            bool ok = true;

            for (size_t i = 0; i < count; ++i) {
                if (p >= end || *p != '$') {
                    if (p >= end) return {{}, 0};
                    ok = false;
                    break;
                }
                ++p; // skip '$'
                size_t bulk_len = 0;
                const char* len_start = p;
                while (p < end && *p >= '0' && *p <= '9') {
                    bulk_len = bulk_len * 10 + static_cast<size_t>(*p - '0');
                    ++p;
                }
                if (p == len_start || p + 1 >= end || *p != '\r' || *(p + 1) != '\n') {
                    if (p + 1 >= end) {
                        return {{}, 0};
                    }
                    ok = false;
                    break;
                }
                p += 2; // skip \r\n

                if (static_cast<size_t>(end - p) < bulk_len + 2) {
                    return {{}, 0}; // Incomplete payload
                }
                if (*(p + bulk_len) != '\r' || *(p + bulk_len + 1) != '\n') {
                    ok = false;
                    break;
                }

                tokens.emplace_back(p, bulk_len);
                p += bulk_len + 2;
            }

            if (ok && tokens.size() == count) {
                size_t consumed = static_cast<size_t>(p - buffer.data());
                return {std::move(tokens), consumed};
            }
        } else if (p + 1 >= end) {
            return {{}, 0};
        }
    }

    RESPValue val;
    size_t consumed = 0;
    auto result = RESPProcessor::decode_one(buffer, val, consumed);
    if (result.status != RESPParseStatus::Complete) {
        return {{}, 0};
    }

    std::vector<std::string> tokens;
    if (val.type() == RESPValue::Type::Array) {
        tokens.reserve(val.array().size());
        for (const auto& item : val.array()) {
            tokens.push_back(item.string());
        }
    } else if (!val.string().empty()) {
        tokens.push_back(val.string());
    }

    return {tokens, consumed};
}

std::string RespParser::encode_simple_string(std::string_view str) {
    return RESPProcessor::serialize_simple_string(str);
}

std::string RespParser::encode_error(std::string_view err) {
    return RESPProcessor::serialize_error(err);
}

std::string RespParser::encode_integer(int64_t val) {
    return RESPProcessor::serialize_integer(val);
}

std::string RespParser::encode_bulk_string(std::string_view str) {
    return RESPProcessor::serialize_bulk_string(str);
}

std::string RespParser::encode_array(const std::vector<std::string>& elements) {
    return RESPProcessor::serialize_array(elements);
}

} // namespace rundb::protocol
