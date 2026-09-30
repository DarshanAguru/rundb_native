cmake_minimum_required(VERSION 3.25)

project(
    rundb
    VERSION 0.1.0
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

# --------------------------------------------------
# Dependencies
# --------------------------------------------------

find_package(quill CONFIG REQUIRED)

# --------------------------------------------------
# RunDB Executable Target
# --------------------------------------------------

add_executable(rundb
    src/main.cpp
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
    src/core/evaluator.cpp
    src/core/store.cpp
    src/server/client.cpp
    src/server/shutdown.cpp
    src/server/server.cpp
    src/protocol/resp_parser.cpp
    src/protocol/resp.cpp
    src/util/printer.cpp
)

# --------------------------------------------------
# Include Directories
# --------------------------------------------------

target_include_directories(rundb
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

# --------------------------------------------------
# Linked Libraries & Bundled jemalloc
# --------------------------------------------------

target_link_libraries(rundb
    PRIVATE
        quill::quill
)

set(BUNDLED_JEMALLOC "${PROJECT_SOURCE_DIR}/lib/libjemalloc.so")

if(EXISTS "${BUNDLED_JEMALLOC}")
    message(STATUS "Found bundled jemalloc: ${BUNDLED_JEMALLOC}")

    add_library(bundled_jemalloc SHARED IMPORTED)
    set_target_properties(bundled_jemalloc PROPERTIES
        IMPORTED_LOCATION "${BUNDLED_JEMALLOC}"
    )

    target_link_libraries(rundb PRIVATE bundled_jemalloc)
    target_compile_definitions(rundb PRIVATE RUNDB_USE_JEMALLOC=1)

    # Set RPATH so the runtime dynamic loader always finds libjemalloc.so
    # next to the executable ($ORIGIN) or in the source lib directory
    set_target_properties(rundb PROPERTIES
        BUILD_RPATH "\$ORIGIN;\$ORIGIN/../lib;${PROJECT_SOURCE_DIR}/lib"
        INSTALL_RPATH "\$ORIGIN;\$ORIGIN/../lib"
    )

    # Copy libjemalloc.so next to the target executable upon successful build
    add_custom_command(TARGET rundb POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${BUNDLED_JEMALLOC}"
            "$<TARGET_FILE_DIR:rundb>/libjemalloc.so"
        COMMENT "Copying bundled libjemalloc.so to output directory"
    )

    install(
        FILES "${BUNDLED_JEMALLOC}"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
else()
    message(STATUS "Bundled jemalloc not found; falling back to system memory allocator")
endif()

# --------------------------------------------------
# Target Compile Options & Warnings
# --------------------------------------------------

target_compile_options(rundb
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        $<$<CONFIG:Release>:-O3 -march=native -fno-omit-frame-pointer>
        $<$<CONFIG:Debug>:-O0 -g3>
)

# --------------------------------------------------
# Installation Rules
# --------------------------------------------------

install(
    TARGETS rundb
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)