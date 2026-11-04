cmake_minimum_required(VERSION 3.28)
project(chat LANGUAGES CXX)

# Этапы 2–4 идут под C++17. На этапе 3 разбираются исключения, на этапе 4 —
# RAII и умные указатели; всё это целиком укладывается в семнадцатый стандарт.
# std::expected появился только в C++23, поэтому второй путь обработки ошибок
# этапа 3 построен на собственном Result.
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
        src/errors.cpp
        src/server.cpp
        src/log.cpp
        src/validate.cpp
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
        tests/test_push_order.cpp
        tests/test_errors.cpp
        tests/test_validate.cpp
        tests/test_guarantee.cpp
        tests/test_server.cpp
        tests/test_result.cpp
        tests/test_fuzz.cpp
        tests/test_members.cpp
        tests/test_log.cpp
        tests/test_cycle.cpp)
target_include_directories(chat_tests PRIVATE tests tools)
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

# Этап 3, второй путь: цена исключений и Result без ошибок и с ошибками.
add_executable(chat_error_cost tools/error_cost.cpp)
target_include_directories(chat_error_cost PRIVATE tools)
target_link_libraries(chat_error_cost PRIVATE chat_core chat_warnings)

# Учебная демонстрация висячего представления. В обычную сборку не входит.
add_executable(chat_dangling_demo EXCLUDE_FROM_ALL tests/dangling_demo.cpp)
target_link_libraries(chat_dangling_demo PRIVATE chat_core chat_warnings)

# Звёздочка этапа 4: цикл из shared_ptr. chat_cycle_demo — исправленный
# вариант, комната держит участников через weak_ptr; входит в обычную сборку.
# chat_cycle_leak — тот же код под флагом CHAT_LEAKING_CYCLE, комната держит
# участников через shared_ptr. В обычную сборку не входит: программа намеренно
# теряет память. Запускать под санитайзером адресов, он покажет утечку.
add_executable(chat_cycle_demo tools/cycle_demo.cpp)
target_include_directories(chat_cycle_demo PRIVATE tools)
target_link_libraries(chat_cycle_demo PRIVATE chat_warnings)

add_executable(chat_cycle_leak EXCLUDE_FROM_ALL tools/cycle_demo.cpp)
target_include_directories(chat_cycle_leak PRIVATE tools)
target_compile_definitions(chat_cycle_leak PRIVATE CHAT_LEAKING_CYCLE)
target_link_libraries(chat_cycle_leak PRIVATE chat_warnings)
