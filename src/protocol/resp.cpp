#include "protocol/resp.hpp"
#include "protocol/resp_parser.hpp"

namespace rundb::protocol {

std::pair<std::vector<std::string>, size_t> RespParser::parse_command(std::string_view buffer) {
    if (buffer.empty()) return {{}, 0};

    RESPValue val;
    size_t consumed = 0;
    auto result = RESPProcessor::decode_one(buffer, val, consumed);
    if (result.status != RESPParseStatus::Complete) {
        return {{}, 0};
    }

    std::vector<std::string> tokens;
    if (val.type() == RESPValue::Type::Array) {
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
