cmake_minimum_required(VERSION 3.28)
project(chat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(chat_warnings INTERFACE)
target_compile_options(chat_warnings INTERFACE -Wall -Wextra -Wshadow)

include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
)
FetchContent_MakeAvailable(googletest)

add_library(
        chat_core
        src/errors.cpp
        src/executor.cpp
        src/file.cpp
        src/handler.cpp
        src/imessage.cpp
        src/lexer.cpp
        src/log.cpp
        src/message.cpp
        src/owning_buffer.cpp
        src/parser.cpp
)
target_include_directories(chat_core PUBLIC include)
target_link_libraries(chat_core PRIVATE chat_warnings)

add_executable(chat_cli src/main.cpp)
target_link_libraries(chat_cli PRIVATE chat_core chat_warnings)

enable_testing()
add_executable(
        chat_tests
        tests/test_chat_store.cpp
        tests/test_errors.cpp
        tests/test_executor.cpp
        tests/test_file.cpp
        tests/test_guarantee.cpp
        tests/test_handler.cpp
        tests/test_hierarchy.cpp
        tests/test_lexer.cpp
        tests/test_lifetime.cpp
        tests/test_log.cpp
        tests/test_members.cpp
        tests/test_message.cpp
        tests/test_owning_buffer.cpp
        tests/test_parser.cpp
)
target_link_libraries(chat_tests PRIVATE chat_core chat_warnings GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(chat_tests)

add_executable(chat_dangling_demo EXCLUDE_FROM_ALL tests/dangling_demo.cpp)
target_link_libraries(chat_dangling_demo PRIVATE chat_core chat_warnings)
