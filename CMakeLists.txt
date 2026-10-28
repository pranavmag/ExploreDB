cmake_minimum_required(VERSION 3.14)

project(ExploreDB)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------
# Core database code
# ---------------------------

set(DB_SOURCES
    src/buffer/buffer_pool_manager.cpp
    src/buffer/clock_replacer.cpp
    src/storage/disk_manager.cpp
    src/buffer/page_table.cpp
    src/storage/heap_page.cpp
    src/storage/heap_file.cpp
    src/storage/page_directory.cpp
)

add_library(db_core ${DB_SOURCES})

target_include_directories(db_core
    PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# ---------------------------
# Main executable
# ---------------------------

add_executable(exploredb
    main.cpp
)

target_link_libraries(exploredb
    PRIVATE
    db_core
)

# ---------------------------
# GoogleTest
# ---------------------------

include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)

FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(db_tests
    tests/disk_manager_tests.cpp
    tests/heap_page_tests.cpp
    tests/heap_file_tests.cpp
    tests/buffer_pool_tests.cpp
    tests/buffer_pool_concurrency_tests.cpp
)

target_link_libraries(db_tests
    PRIVATE
    db_core
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(db_tests)

# ---------------------------
# Benchmarks
# ---------------------------

add_executable(buffer_pool_benchmark
    benchmarks/cpp/buffer_pool_benchmark.cpp
)

target_link_libraries(buffer_pool_benchmark
    PRIVATE
    db_core
)

# ---------------------------
# ThreadSanitizer
# ---------------------------

option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if (ENABLE_TSAN)
    target_compile_options(db_core PRIVATE
        -fsanitize=thread
        -g
    )

    target_link_options(db_core PRIVATE
        -fsanitize=thread
    )

    target_compile_options(db_tests PRIVATE
        -fsanitize=thread
        -g
    )

    target_link_options(db_tests PRIVATE
        -fsanitize=thread
    )
endif()