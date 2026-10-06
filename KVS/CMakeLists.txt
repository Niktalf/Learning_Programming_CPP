cmake_minimum_required(VERSION 3.28)
project(kvs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(kvs_warnings INTERFACE)
target_compile_options(kvs_warnings INTERFACE -Wall -Wextra -Wshadow)

include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
)
FetchContent_MakeAvailable(googletest)

add_library(
        kvs_core
        src/ivalue.cpp
        src/value.cpp
        src/log.cpp
        src/file.cpp
        src/request_cache.cpp
        src/errors.cpp
        src/handler.cpp
        src/lexer.cpp
        src/parser.cpp
        src/executor.cpp
        src/owning_buffer.cpp
)
target_include_directories(kvs_core PUBLIC include)
target_link_libraries(kvs_core PRIVATE kvs_warnings)

add_executable(kvs_cli src/main.cpp)
target_link_libraries(kvs_cli PRIVATE kvs_core kvs_warnings)

enable_testing()
add_executable(
        kvs_tests
        tests/test_value.cpp
        tests/test_owning_buffer.cpp
        tests/test_store.cpp
        tests/test_lexer.cpp
        tests/test_parser.cpp
        tests/test_executor.cpp
        tests/test_lifetime.cpp
        tests/test_errors.cpp
        tests/test_guarantee.cpp
        tests/test_handler.cpp
        tests/test_hierarchy.cpp
        tests/test_log.cpp
        tests/test_file.cpp
        tests/test_cache.cpp
)
target_link_libraries(kvs_tests PRIVATE kvs_core kvs_warnings GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(kvs_tests)

add_executable(kvs_dangling_demo EXCLUDE_FROM_ALL tests/dangling_demo.cpp)
target_link_libraries(kvs_dangling_demo PRIVATE kvs_core kvs_warnings)
