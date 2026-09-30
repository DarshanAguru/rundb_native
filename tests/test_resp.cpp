#include "test_framework.hpp"
#include "protocol/resp_parser.hpp"
#include "protocol/resp.hpp"

using namespace rundb;
using rundb::protocol::RespParser;

TEST_CASE("RESP_EncodingPrimitives") {
    ASSERT_EQ(RespParser::encode_ok(), "+OK\r\n");
    ASSERT_EQ(RespParser::encode_pong(), "+PONG\r\n");
    ASSERT_EQ(RespParser::encode_nil(), "$-1\r\n");
    ASSERT_EQ(RespParser::encode_simple_string("QUEUED"), "+QUEUED\r\n");
    ASSERT_EQ(RespParser::encode_error("ERR test error"), "-ERR test error\r\n");
    ASSERT_EQ(RespParser::encode_integer(42), ":42\r\n");
    ASSERT_EQ(RespParser::encode_integer(-100), ":-100\r\n");
    ASSERT_EQ(RespParser::encode_bulk_string("hello"), "$5\r\nhello\r\n");
    ASSERT_EQ(RespParser::encode_bulk_string(""), "$0\r\n\r\n");
    ASSERT_EQ(RespParser::encode_null_array(), "*-1\r\n");

    std::vector<std::string> arr = {"foo", "bar"};
    ASSERT_EQ(RespParser::encode_array(arr), "*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n");
}

TEST_CASE("RESP_ParseSimpleStringAndError") {
    auto res_ok = RESPProcessor::decode("+OK\r\n");
    ASSERT_EQ(static_cast<int>(res_ok.status), static_cast<int>(RESPParseStatus::Complete));
    ASSERT_EQ(res_ok.values.size(), 1);
    ASSERT_EQ(static_cast<int>(res_ok.values[0].type()), static_cast<int>(RESPValue::Type::SimpleString));
    ASSERT_EQ(res_ok.values[0].string(), "OK");

    auto res_err = RESPProcessor::decode("-ERR unknown command\r\n");
    ASSERT_EQ(static_cast<int>(res_err.status), static_cast<int>(RESPParseStatus::Complete));
    ASSERT_EQ(res_err.values.size(), 1);
    ASSERT_EQ(static_cast<int>(res_err.values[0].type()), static_cast<int>(RESPValue::Type::Error));
    ASSERT_EQ(res_err.values[0].string(), "ERR unknown command");
}

TEST_CASE("RESP_ParseInteger") {
    auto res = RESPProcessor::decode(":12345\r\n");
    ASSERT_EQ(static_cast<int>(res.status), static_cast<int>(RESPParseStatus::Complete));
    ASSERT_EQ(res.values.size(), 1);
    ASSERT_EQ(static_cast<int>(res.values[0].type()), static_cast<int>(RESPValue::Type::Integer));
    ASSERT_EQ(res.values[0].integer(), 12345);

    auto res_neg = RESPProcessor::decode(":-999\r\n");
    ASSERT_EQ(res_neg.values[0].integer(), -999);
}

TEST_CASE("RESP_ParseBulkStringAndNull") {
    auto res = RESPProcessor::decode("$5\r\nworld\r\n");
    ASSERT_EQ(static_cast<int>(res.status), static_cast<int>(RESPParseStatus::Complete));
    ASSERT_EQ(res.values.size(), 1);
    ASSERT_EQ(static_cast<int>(res.values[0].type()), static_cast<int>(RESPValue::Type::BulkString));
    ASSERT_EQ(res.values[0].string(), "world");

    // Null bulk string
    auto res_nil = RESPProcessor::decode("$-1\r\n");
    ASSERT_EQ(static_cast<int>(res_nil.status), static_cast<int>(RESPParseStatus::Complete));
    ASSERT_EQ(static_cast<int>(res_nil.values[0].type()), static_cast<int>(RESPValue::Type::Null));
}

TEST_CASE("RESP_ParseArrayCommand") {
    // Array: *3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n
    std::string raw = "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n";
    auto res = RESPProcessor::decode(raw);
    ASSERT_EQ(static_cast<int>(res.status), static_cast<int>(RESPParseStatus::Complete));
    ASSERT_EQ(res.values.size(), 1);
    ASSERT_EQ(static_cast<int>(res.values[0].type()), static_cast<int>(RESPValue::Type::Array));
    ASSERT_EQ(res.values[0].array().size(), 3);

    Cmd cmd = RESPProcessor::to_cmd(res.values[0]);
    ASSERT_EQ(cmd.command, "SET");
    ASSERT_EQ(cmd.args.size(), 2);
    ASSERT_EQ(cmd.args[0], "foo");
    ASSERT_EQ(cmd.args[1], "bar");
}

TEST_CASE("RESP_ParseInlineCommand") {
    // Plain text inline command (e.g. netcat/telnet)
    auto res = RESPProcessor::decode("PING\r\n");
    ASSERT_EQ(static_cast<int>(res.status), static_cast<int>(RESPParseStatus::Complete));
    ASSERT_EQ(res.values.size(), 1);
    Cmd cmd = RESPProcessor::to_cmd(res.values[0]);
    ASSERT_EQ(cmd.command, "PING");

    auto res2 = RESPProcessor::decode("SET key value\r\n");
    Cmd cmd2 = RESPProcessor::to_cmd(res2.values[0]);
    ASSERT_EQ(cmd2.command, "SET");
    ASSERT_EQ(cmd2.args.size(), 2);
    ASSERT_EQ(cmd2.args[0], "key");
    ASSERT_EQ(cmd2.args[1], "value");
}

TEST_CASE("RESP_PipelinedCommands") {
    std::string pipeline = "*1\r\n$4\r\nPING\r\n*2\r\n$4\r\nECHO\r\n$5\r\nHELLO\r\n";
    auto res = RESPProcessor::decode(pipeline);
    ASSERT_EQ(static_cast<int>(res.status), static_cast<int>(RESPParseStatus::Complete));
    ASSERT_EQ(res.values.size(), 2);

    Cmd cmd1 = RESPProcessor::to_cmd(res.values[0]);
    ASSERT_EQ(cmd1.command, "PING");

    Cmd cmd2 = RESPProcessor::to_cmd(res.values[1]);
    ASSERT_EQ(cmd2.command, "ECHO");
    ASSERT_EQ(cmd2.args.size(), 1);
    ASSERT_EQ(cmd2.args[0], "HELLO");
}

TEST_CASE("RESP_IncompleteBuffer") {
    // Partial bulk string
    std::string partial = "$10\r\nhello";
    auto res = RESPProcessor::decode(partial);
    ASSERT_EQ(static_cast<int>(res.status), static_cast<int>(RESPParseStatus::Incomplete));
    ASSERT_EQ(res.values.size(), 0);

    // Partial array
    std::string partial_arr = "*2\r\n$3\r\nfoo\r\n";
    auto res_arr = RESPProcessor::decode(partial_arr);
    ASSERT_EQ(static_cast<int>(res_arr.status), static_cast<int>(RESPParseStatus::Incomplete));
}
