cmake_minimum_required(VERSION 3.28)
project(chat LANGUAGES CXX)

# Этап 2 идёт под C++17: на лекции 2 разбирались string_view, optional,
# variant, structured bindings и if с инициализацией. Ровно это здесь и
# применяется, ни строчкой новее.
#
# Стандарт поднимается вместе с курсом: этап 1 был под 14, этапы 3–4 остаются
# на 17, с этапа 5 — C++23. Так компилятор сам не даёт применить то, чего ещё
# не было на лекциях.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Общие для своих целей предупреждения. Намеренно не через CMAKE_CXX_FLAGS:
# иначе они распространятся и на исходники GoogleTest, загруженные через
# FetchContent, и их придётся отключать.
add_library(chat_warnings INTERFACE)
target_compile_options(chat_warnings INTERFACE -Wall -Wextra -Wshadow)

include(FetchContent)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2)
FetchContent_MakeAvailable(googletest)

add_library(chat_core
        src/message.cpp
        src/lexer.cpp
        src/parser.cpp
        src/executor.cpp
        src/owning_buffer.cpp)
target_include_directories(chat_core PUBLIC include)
target_link_libraries(chat_core PRIVATE chat_warnings)

add_executable(chat_cli src/main.cpp)
target_link_libraries(chat_cli PRIVATE chat_core chat_warnings)

enable_testing()
add_executable(chat_tests
        tests/test_message.cpp
        tests/test_owning_buffer.cpp
        tests/test_chat_store.cpp
        tests/test_lexer.cpp
        tests/test_parser.cpp
        tests/test_executor.cpp
        tests/test_lifetime.cpp
        tests/test_push_order.cpp)
target_include_directories(chat_tests PRIVATE tests)
target_link_libraries(chat_tests PRIVATE chat_core chat_warnings GTest::gtest_main)

# Звёздочка этапа 1: порядок конструкторов при добавлении в std::vector.
add_executable(chat_push_order tools/push_order.cpp)
target_include_directories(chat_push_order PRIVATE tests)
target_link_libraries(chat_push_order PRIVATE chat_warnings)

# Звёздочка этапа 2: аллокации при разборе на std::string и std::string_view.
# Подмена operator new живёт только в этих двух программах.
add_executable(chat_parse_allocations tools/parse_allocations.cpp tools/alloc_counter.cpp)
target_include_directories(chat_parse_allocations PRIVATE tools)
target_link_libraries(chat_parse_allocations PRIVATE chat_core chat_warnings)

add_executable(chat_alloc_tests tests/test_allocations.cpp tools/alloc_counter.cpp)
target_include_directories(chat_alloc_tests PRIVATE tools)
target_link_libraries(chat_alloc_tests PRIVATE chat_core chat_warnings GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(chat_tests)
gtest_discover_tests(chat_alloc_tests)

# Учебная демонстрация висячего представления. В обычную сборку не входит.
add_executable(chat_dangling_demo EXCLUDE_FROM_ALL tests/dangling_demo.cpp)
target_link_libraries(chat_dangling_demo PRIVATE chat_core chat_warnings)
