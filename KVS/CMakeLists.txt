cmake_minimum_required(VERSION 3.28)
project(kvs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(kvs_warnings INTERFACE)
target_compile_options(kvs_warnings INTERFACE -Wall -Wextra -Wshadow)

include(FetchContent)
FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2)
FetchContent_MakeAvailable(googletest)

add_library(
        kvs_core
        src/value.cpp
        src/owning_buffer.cpp
)
target_include_directories(kvs_core PUBLIC include)
target_link_libraries(kvs_core PRIVATE kvs_warnings)

add_executable(kvs_cli src/main.cpp)
target_link_libraries(kvs_cli PRIVATE kvs_core kvs_warnings)

enable_testing()
add_executable(kvs_tests
        tests/test_value.cpp
        tests/test_owning_buffer.cpp
        tests/test_store.cpp)
target_link_libraries(kvs_tests PRIVATE kvs_core kvs_warnings GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(kvs_tests)