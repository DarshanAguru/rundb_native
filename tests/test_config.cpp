#include "test_framework.hpp"
#include "argparse.hpp"
#include "config.hpp"

using namespace rundb;

TEST_CASE("Config_DefaultArguments") {
    char prog[] = "rundb";
    char* argv[] = {prog};
    int argc = 1;

    Args args = ArgParse::parse(argc, argv);
    ASSERT_EQ(args.port, 7379);
    ASSERT_EQ(args.host, "127.0.0.1");
    ASSERT_EQ(args.log_level, "INFO");
    ASSERT_EQ(args.eviction_strategy, "noeviction");
    ASSERT_EQ(args.memory_limit, 0);
    ASSERT_TRUE(args.aof_enabled);
}

TEST_CASE("Config_ExplicitCLIFlags") {
    char prog[] = "rundb";
    char p_flag[] = "--port";
    char port[] = "8080";
    char h_flag[] = "--host";
    char host[] = "0.0.0.0";
    char mem_flag[] = "--memory-limit";
    char mem[] = "52428800";
    char evict_flag[] = "--eviction-strategy";
    char evict[] = "allkeys-lru";
    char aof_flag[] = "--aof-enabled";
    char aof_val[] = "yes";

    char* argv[] = {
        prog, p_flag, port, h_flag, host, mem_flag, mem, evict_flag, evict, aof_flag, aof_val
    };
    int argc = sizeof(argv) / sizeof(argv[0]);

    Args args = ArgParse::parse(argc, argv);
    ASSERT_EQ(args.port, 8080);
    ASSERT_EQ(args.host, "0.0.0.0");
    ASSERT_EQ(args.memory_limit, 52428800);
    ASSERT_EQ(args.eviction_strategy, "allkeys-lru");
    ASSERT_TRUE(args.aof_enabled);
    ASSERT_TRUE(args.has_port);
    ASSERT_TRUE(args.has_host);
    ASSERT_TRUE(args.has_memory_limit);
}

TEST_CASE("Config_InvalidArgumentsThrow") {
    char prog[] = "rundb";
    char invalid_flag[] = "--unknown-bogus-flag";
    char* argv[] = {prog, invalid_flag};
    int argc = 2;

    ASSERT_THROWS(ArgParse::parse(argc, argv), std::runtime_error);
}

TEST_CASE("Config_AofEnabledStandaloneFlag") {
    // 1. Standalone flag without following value
    char prog[] = "rundb";
    char aof_flag[] = "--aof-enabled";
    char* argv1[] = {prog, aof_flag};
    Args args1 = ArgParse::parse(2, argv1);
    ASSERT_TRUE(args1.aof_enabled);
    ASSERT_TRUE(args1.has_aof_enabled);

    // 2. Standalone flag followed by another option
    char port_flag[] = "--port";
    char port_val[] = "6379";
    char* argv2[] = {prog, aof_flag, port_flag, port_val};
    Args args2 = ArgParse::parse(4, argv2);
    ASSERT_TRUE(args2.aof_enabled);
    ASSERT_TRUE(args2.has_aof_enabled);
    ASSERT_EQ(args2.port, 6379);

    // 3. Flag with explicit "no"
    char no_val[] = "no";
    char* argv3[] = {prog, aof_flag, no_val};
    Args args3 = ArgParse::parse(3, argv3);
    ASSERT_FALSE(args3.aof_enabled);
    ASSERT_TRUE(args3.has_aof_enabled);
}

TEST_CASE("Config_NoAofFlag") {
    char prog[] = "rundb";
    char no_aof_flag[] = "--no-aof";
    char* argv[] = {prog, no_aof_flag};
    Args args = ArgParse::parse(2, argv);
    ASSERT_FALSE(args.aof_enabled);
    ASSERT_TRUE(args.has_aof_enabled);
}
