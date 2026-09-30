#include "test_framework.hpp"
#include "logger.hpp"

int main(int argc, char* argv[]) {
    // Initialize logger at Warning level to avoid test log spam while keeping macros functional
    rundb::Logger::init("WARNING");

    int result = rundb::testing::TestRegistry::instance().run_all(argc, argv);

    rundb::Logger::shutdown();
    return result;
}
