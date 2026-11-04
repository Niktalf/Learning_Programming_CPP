cmake_minimum_required(VERSION 3.28)
project(kvs LANGUAGES CXX)

# Этапы 2–4 идут под C++17. На этапе 3 разбираются исключения, на этапе 4 —
# RAII и умные указатели; всё это целиком укладывается в семнадцатый стандарт.
# Единственное, чего здесь не хватает, — make_unique_for_overwrite из C++20,
# см. комментарий в src/owning_buffer.cpp.
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
add_library(kvs_warnings INTERFACE)
target_compile_options(kvs_warnings INTERFACE -Wall -Wextra -Wshadow)

include(FetchContent)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2)
FetchContent_MakeAvailable(googletest)

add_library(kvs_core
        src/ivalue.cpp
        src/value.cpp
        src/errors.cpp
        src/log.cpp
        src/file.cpp
        src/request_cache.cpp
        src/server.cpp
        src/lexer.cpp
        src/parser.cpp
        src/executor.cpp
        src/owning_buffer.cpp)
target_include_directories(kvs_core PUBLIC include)
target_link_libraries(kvs_core PRIVATE kvs_warnings)

add_executable(kvs_cli src/main.cpp)
target_link_libraries(kvs_cli PRIVATE kvs_core kvs_warnings)

enable_testing()
add_executable(kvs_tests
        tests/test_value.cpp
        tests/test_owning_buffer.cpp
        tests/test_store.cpp
        tests/test_insert_order.cpp
        tests/test_lexer.cpp
        tests/test_parser.cpp
        tests/test_executor.cpp
        tests/test_lifetime.cpp
        tests/test_errors.cpp
        tests/test_guarantee.cpp
        tests/test_server.cpp
        tests/test_result.cpp
        tests/test_fuzz.cpp
        tests/test_hierarchy.cpp
        tests/test_log.cpp
        tests/test_file.cpp
        tests/test_cache.cpp
        tests/test_cycle.cpp)
target_include_directories(kvs_tests PRIVATE tools)
target_link_libraries(kvs_tests PRIVATE kvs_core kvs_warnings GTest::gtest_main)

# Звёздочка: порядок вызовов конструкторов при вставке в std::map.
add_executable(kvs_insert_order tools/insert_order.cpp)
target_include_directories(kvs_insert_order PRIVATE tests)
target_link_libraries(kvs_insert_order PRIVATE kvs_warnings)

# Звёздочка этапа 2: аллокации при разборе на std::string и std::string_view.
# Подмена operator new живёт только в этих двух программах.
add_executable(kvs_parse_allocations tools/parse_allocations.cpp tools/alloc_counter.cpp)
target_include_directories(kvs_parse_allocations PRIVATE tools)
target_link_libraries(kvs_parse_allocations PRIVATE kvs_core kvs_warnings)

add_executable(kvs_alloc_tests tests/test_allocations.cpp tools/alloc_counter.cpp)
target_include_directories(kvs_alloc_tests PRIVATE tools)
target_link_libraries(kvs_alloc_tests PRIVATE kvs_core kvs_warnings GTest::gtest_main)

# Этап 3, второй путь: цена исключений и Result без ошибок и с ошибками.
add_executable(kvs_error_cost tools/error_cost.cpp)
target_include_directories(kvs_error_cost PRIVATE tools)
target_link_libraries(kvs_error_cost PRIVATE kvs_core kvs_warnings)

include(GoogleTest)
gtest_discover_tests(kvs_tests)
gtest_discover_tests(kvs_alloc_tests)

# Учебная демонстрация висячего представления. В обычную сборку не входит:
# программа намеренно обращается к освобождённой памяти. Запускать под
# санитайзером адресов.
add_executable(kvs_dangling_demo EXCLUDE_FROM_ALL tests/dangling_demo.cpp)
target_link_libraries(kvs_dangling_demo PRIVATE kvs_core kvs_warnings)

# Звёздочка этапа 4: цикл из shared_ptr. kvs_cycle_demo — исправленный
# вариант с обратной ссылкой на weak_ptr, входит в обычную сборку.
# kvs_cycle_leak — тот же код с обратной ссылкой на shared_ptr, собранный под
# флагом KVS_LEAKING_CYCLE. В обычную сборку не входит: программа намеренно
# теряет память. Запускать под санитайзером адресов, он покажет утечку.
add_executable(kvs_cycle_demo tools/cycle_demo.cpp)
target_include_directories(kvs_cycle_demo PRIVATE tools)
target_link_libraries(kvs_cycle_demo PRIVATE kvs_warnings)

add_executable(kvs_cycle_leak EXCLUDE_FROM_ALL tools/cycle_demo.cpp)
target_include_directories(kvs_cycle_leak PRIVATE tools)
target_compile_definitions(kvs_cycle_leak PRIVATE KVS_LEAKING_CYCLE)
target_link_libraries(kvs_cycle_leak PRIVATE kvs_warnings)
