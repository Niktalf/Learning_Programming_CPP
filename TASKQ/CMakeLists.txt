cmake_minimum_required(VERSION 3.28)
project(taskq LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(taskq_warnings INTERFACE)
target_compile_options(taskq_warnings INTERFACE -Wall -Wextra -Wshadow)

include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
)
FetchContent_MakeAvailable(googletest)

add_library(
        taskq_core
        src/errors.cpp
        src/executor.cpp
        src/file.cpp
        src/handler.cpp
        src/ipayload.cpp
        src/lexer.cpp
        src/log.cpp
        src/owning_buffer.cpp
        src/parser.cpp
        src/state.cpp
        src/task.cpp
)
target_include_directories(taskq_core PUBLIC include)
target_link_libraries(taskq_core PRIVATE taskq_warnings)

add_executable(taskq_cli src/main.cpp)
target_link_libraries(taskq_cli PRIVATE taskq_core taskq_warnings)

enable_testing()
add_executable(
        taskq_tests
        tests/test_errors.cpp
        tests/test_executor.cpp
        tests/test_file.cpp
        tests/test_guarantee.cpp
        tests/test_handler.cpp
        tests/test_hierarchy.cpp
        tests/test_lease.cpp
        tests/test_lexer.cpp
        tests/test_lifetime.cpp
        tests/test_log.cpp
        tests/test_owning_buffer.cpp
        tests/test_parser.cpp
        tests/test_state.cpp
        tests/test_task.cpp
        tests/test_task_store.cpp
)
target_link_libraries(taskq_tests PRIVATE taskq_core taskq_warnings GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(taskq_tests)

add_executable(taskq_dangling_demo EXCLUDE_FROM_ALL tests/dangling_demo.cpp)
target_link_libraries(taskq_dangling_demo PRIVATE taskq_core taskq_warnings)
