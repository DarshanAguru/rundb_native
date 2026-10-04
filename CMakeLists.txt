cmake_minimum_required(VERSION 3.25)

# Read version automatically from include/version.hpp (Single Source of Truth)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/version.hpp" _version_line REGEX "inline constexpr std::string_view VERSION = \"([0-9]+\\.[0-9]+\\.[0-9]+)\";")
if(_version_line MATCHES "\"([0-9]+\\.[0-9]+\\.[0-9]+)\"")
    set(RUNDB_VERSION_STRING "${CMAKE_MATCH_1}")
else()
    set(RUNDB_VERSION_STRING "0.1.0")
endif()

project(
    rundb
    VERSION ${RUNDB_VERSION_STRING}
    DESCRIPTION "Redis-inspired in-memory database written in modern C++"
    LANGUAGES CXX
)

# --------------------------------------------------
# C++ Standard Configuration
# --------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

# Enable Link Time Optimization (LTO) in Release mode for maximum inlining
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

# Global RPATH configuration so executables find bundled shared libraries
set(CMAKE_BUILD_RPATH "\$ORIGIN;\$ORIGIN/../lib;${PROJECT_SOURCE_DIR}/lib")
set(CMAKE_INSTALL_RPATH "\$ORIGIN;\$ORIGIN/../lib")

# --------------------------------------------------
# Dependencies
# --------------------------------------------------

find_package(quill CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

# --------------------------------------------------
# Core Library Target (Reusable by Server, Tests, Benchmarks)
# --------------------------------------------------

add_library(rundb_core STATIC
    src/logger.cpp
    src/config.cpp
    src/syscheck.cpp
    src/argparse.cpp
    src/core/internals/run_alloc.cpp
    src/core/internals/sds.cpp
    src/core/internals/intset.cpp
    src/core/internals/quicklist.cpp
    src/core/object.cpp
    src/core/database.cpp
    src/core/evict_pool.cpp
    src/core/eviction.cpp
    src/core/expiration.cpp
    src/core/stats.cpp
    src/core/aof.cpp
    src/core/snapshot.cpp
    src/core/evaluator.cpp
    src/core/store.cpp
    src/server/client.cpp
    src/server/shutdown.cpp
    src/server/server.cpp
    src/protocol/resp_parser.cpp
    src/protocol/resp.cpp
    src/util/printer.cpp
)

target_include_directories(rundb_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(rundb_core
    PUBLIC
        quill::quill
        ZLIB::ZLIB
)

set(BUNDLED_JEMALLOC "${PROJECT_SOURCE_DIR}/lib/libjemalloc.so")

if(EXISTS "${BUNDLED_JEMALLOC}")
    message(STATUS "Found bundled jemalloc: ${BUNDLED_JEMALLOC}")

    add_library(bundled_jemalloc SHARED IMPORTED)
    set_target_properties(bundled_jemalloc PROPERTIES
        IMPORTED_LOCATION "${BUNDLED_JEMALLOC}"
    )

    target_link_libraries(rundb_core PUBLIC bundled_jemalloc)
    target_compile_definitions(rundb_core PUBLIC RUNDB_USE_JEMALLOC=1)

    install(
        FILES "${BUNDLED_JEMALLOC}" "${PROJECT_SOURCE_DIR}/lib/libjemalloc.so.2"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
else()
    message(STATUS "Bundled jemalloc not found; falling back to system memory allocator")
endif()

target_compile_options(rundb_core
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        $<$<CONFIG:Release>:-O3 -march=native -fno-omit-frame-pointer>
        $<$<CONFIG:Debug>:-O0 -g3>
)

# --------------------------------------------------
# RunDB Server Executable Target
# --------------------------------------------------

add_executable(rundb
    src/main.cpp
)

target_link_libraries(rundb
    PRIVATE
        rundb_core
)

target_compile_options(rundb
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        $<$<CONFIG:Release>:-O3 -march=native -fno-omit-frame-pointer>
        $<$<CONFIG:Debug>:-O0 -g3>
)

# --------------------------------------------------
# Native C++ Unit & Integration Tests Target
# --------------------------------------------------

enable_testing()

add_executable(rundb_tests
    tests/test_main.cpp
    tests/test_alloc.cpp
    tests/test_sds.cpp
    tests/test_intset.cpp
    tests/test_quicklist.cpp
    tests/test_resp.cpp
    tests/test_database.cpp
    tests/test_expiration.cpp
    tests/test_eviction.cpp
    tests/test_evaluator.cpp
    tests/test_aof.cpp
    tests/test_snapshot.cpp
    tests/test_config.cpp
    tests/test_network.cpp
)

target_include_directories(rundb_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/tests
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(rundb_tests
    PRIVATE
        rundb_core
)

target_compile_options(rundb_tests
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        $<$<CONFIG:Release>:-O3 -march=native -fno-omit-frame-pointer>
        $<$<CONFIG:Debug>:-O0 -g3>
)

add_test(NAME rundb_tests COMMAND rundb_tests)

# --------------------------------------------------
# Native C++ Benchmarking Suite Target
# --------------------------------------------------

add_executable(rundb_benchmark
    tests/benchmark_main.cpp
)

target_include_directories(rundb_benchmark
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(rundb_benchmark
    PRIVATE
        rundb_core
)

target_compile_options(rundb_benchmark
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        $<$<CONFIG:Release>:-O3 -march=native -fno-omit-frame-pointer>
        $<$<CONFIG:Debug>:-O0 -g3>
)

# Copy libjemalloc.so and libjemalloc.so.2 next to target executables upon successful build
if(EXISTS "${BUNDLED_JEMALLOC}")
    foreach(_target rundb rundb_tests rundb_benchmark)
        add_custom_command(TARGET ${_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${BUNDLED_JEMALLOC}"
                "$<TARGET_FILE_DIR:${_target}>/libjemalloc.so"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${BUNDLED_JEMALLOC}"
                "$<TARGET_FILE_DIR:${_target}>/libjemalloc.so.2"
            COMMENT "Copying bundled libjemalloc.so & libjemalloc.so.2 for ${_target}"
        )
    endforeach()
endif()

# --------------------------------------------------
# Installation Rules
# --------------------------------------------------

install(
    TARGETS rundb rundb_tests rundb_benchmark
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)