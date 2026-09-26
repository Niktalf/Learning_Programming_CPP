cmake_minimum_required(VERSION 3.28)
project(chat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(chat_warnings INTERFACE)
target_compile_options(chat_warnings INTERFACE -Wall -Wextra -Wshadow)

include(FetchContent)
FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2)
FetchContent_MakeAvailable(googletest)

add_library(chat_core
        src/message.cpp
        src/owning_buffer.cpp)
target_include_directories(chat_core PUBLIC include)
target_link_libraries(chat_core PRIVATE chat_warnings)

add_executable(chat_cli src/main.cpp)
target_link_libraries(chat_cli PRIVATE chat_core chat_warnings)

enable_testing()
add_executable(chat_tests
        tests/test_message.cpp
        tests/test_owning_buffer.cpp
        tests/test_chat_store.cpp)
target_link_libraries(chat_tests PRIVATE chat_core chat_warnings GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(chat_tests)
