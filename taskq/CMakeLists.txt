cmake_minimum_required(VERSION 3.28)
project(taskq LANGUAGES CXX)

# Этап 3 идёт под C++17: исключения, гарантии безопасности и noexcept
# укладываются в семнадцатый стандарт. std::expected появился только в C++23,
# поэтому второй путь обработки ошибок построен на собственном Result.
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
add_library(taskq_warnings INTERFACE)
target_compile_options(taskq_warnings INTERFACE -Wall -Wextra -Wshadow)

include(FetchContent)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2)
FetchContent_MakeAvailable(googletest)

add_library(taskq_core
        src/task.cpp
        src/lexer.cpp
        src/parser.cpp
        src/executor.cpp
        src/errors.cpp
        src/handler.cpp
        src/validate.cpp
        src/owning_buffer.cpp)
target_include_directories(taskq_core PUBLIC include)
target_link_libraries(taskq_core PRIVATE taskq_warnings)

add_executable(taskq_cli src/main.cpp)
target_link_libraries(taskq_cli PRIVATE taskq_core taskq_warnings)

enable_testing()
add_executable(taskq_tests
        tests/test_task.cpp
        tests/test_owning_buffer.cpp
        tests/test_task_store.cpp
        tests/test_lexer.cpp
        tests/test_parser.cpp
        tests/test_executor.cpp
        tests/test_lifetime.cpp
        tests/test_push_order.cpp
        tests/test_errors.cpp
        tests/test_state.cpp
        tests/test_validate.cpp
        tests/test_guarantee.cpp
        tests/test_handler.cpp
        tests/test_result.cpp
        tests/test_fuzz.cpp)
target_include_directories(taskq_tests PRIVATE tests)
target_link_libraries(taskq_tests PRIVATE taskq_core taskq_warnings GTest::gtest_main)

# Звёздочка этапа 1: порядок конструкторов при добавлении в std::vector.
add_executable(taskq_push_order tools/push_order.cpp)
target_include_directories(taskq_push_order PRIVATE tests)
target_link_libraries(taskq_push_order PRIVATE taskq_warnings)

# Звёздочка этапа 2: аллокации при разборе на std::string и std::string_view.
# Подмена operator new живёт только в этих двух программах.
add_executable(taskq_parse_allocations tools/parse_allocations.cpp tools/alloc_counter.cpp)
target_include_directories(taskq_parse_allocations PRIVATE tools)
target_link_libraries(taskq_parse_allocations PRIVATE taskq_core taskq_warnings)

add_executable(taskq_alloc_tests tests/test_allocations.cpp tools/alloc_counter.cpp)
target_include_directories(taskq_alloc_tests PRIVATE tools)
target_link_libraries(taskq_alloc_tests PRIVATE taskq_core taskq_warnings GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(taskq_tests)
gtest_discover_tests(taskq_alloc_tests)

# Этап 3, второй путь: цена исключений и Result без ошибок и с ошибками.
add_executable(taskq_error_cost tools/error_cost.cpp)
target_include_directories(taskq_error_cost PRIVATE tools)
target_link_libraries(taskq_error_cost PRIVATE taskq_core taskq_warnings)

# Учебная демонстрация висячего представления. В обычную сборку не входит.
add_executable(taskq_dangling_demo EXCLUDE_FROM_ALL tests/dangling_demo.cpp)
target_link_libraries(taskq_dangling_demo PRIVATE taskq_core taskq_warnings)
