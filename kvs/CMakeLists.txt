cmake_minimum_required(VERSION 3.28)
project(kvs LANGUAGES CXX)

# С этапа 5 — C++23. На лекции 5 разбирались концепции, ranges, оператор <=>
# и корутины; вместе с двадцать третьим стандартом приходят std::expected
# (он заменяет собственный Result этапа 3), std::print, std::generator и
# ranges::to.
#
# Лестница стандартов: этап 1 — C++14, этапы 2–4 — C++17, с этапа 5 — C++23.
# Так компилятор сам не даёт применить то, чего ещё не было на лекциях.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# CMake с C++20 и новее по умолчанию сканирует каждый исходник в поисках
# модулей. Модули в проекте — только эксперимент домашнего задания этапа 5,
# поэтому сканирование выключено везде и включается лишь для его цели.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

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
        src/glob.cpp
        src/bytes.cpp
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
        tests/test_cycle.cpp
        tests/test_concepts.cpp
        tests/test_keys.cpp
        tests/test_version.cpp
        tests/test_generator.cpp)
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

# --- Этап 5: программы, которые не должны собираться -----------------------
#
# Каждая — отдельная цель вне обычной сборки и тест, который пытается её
# собрать и проверяет, что сборка упала, причём по ожидаемой причине: вывод
# компилятора сверяется с образцом. Так «не компилируется» становится
# проверяемым утверждением, а не словами в README.
function(kvs_expect_compile_failure name source expect)
    add_executable(${name} EXCLUDE_FROM_ALL ${source})
    target_link_libraries(${name} PRIVATE kvs_core kvs_warnings)
    add_test(NAME ${name}
             COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -DTARGET=${name}
                     "-DEXPECT=${expect}"
                     -P ${CMAKE_SOURCE_DIR}/cmake/expect_compile_failure.cmake)
endfunction()

# Конвейер над временным контейнером: ranges::find возвращает
# std::ranges::dangling вместо итератора, и разыменовать его нельзя.
kvs_expect_compile_failure(kvs_dangling_range_fail tests/compile_fail/dangling_range.cpp
                           "dangling")

# Одна и та же ошибка — нарушение требования к ключу — с концепцией и с
# enable_if. Сравнение объёма диагностики — в docs/concepts.md.
kvs_expect_compile_failure(kvs_concept_diagnostics_fail tests/compile_fail/concept_key.cpp
                           "KeyLike")
kvs_expect_compile_failure(kvs_enable_if_diagnostics_fail tests/compile_fail/enable_if_key.cpp
                           "enable_if")

# Та же пара на стандартной библиотеке, где разница в объёме видна лучше всего:
# std::sort и std::ranges::sort над std::list.
kvs_expect_compile_failure(kvs_sort_classic_fail tests/compile_fail/sort_classic.cpp
                           "operator-")
kvs_expect_compile_failure(kvs_sort_ranges_fail tests/compile_fail/sort_ranges.cpp
                           "random_access")

# Домашнее задание этапа 5: модули. Включаются явно, -DKVS_MODULES=ON:
# поддержка в CMake, Ninja и компиляторах всё ещё неровная, и обычная сборка
# от неё зависеть не должна. Что получилось и что нет — docs/modules.md.
option(KVS_MODULES "Build the C++ modules experiment (stage 5 homework)" OFF)
if(KVS_MODULES)
    add_library(kvs_glob_module)
    set_target_properties(kvs_glob_module PROPERTIES CXX_SCAN_FOR_MODULES ON)
    target_sources(kvs_glob_module PUBLIC FILE_SET CXX_MODULES FILES modules/kvs.glob.cppm)
    target_link_libraries(kvs_glob_module PRIVATE kvs_warnings)
    add_executable(kvs_modules_demo modules/demo.cpp)
    set_target_properties(kvs_modules_demo PROPERTIES CXX_SCAN_FOR_MODULES ON)
    target_link_libraries(kvs_modules_demo PRIVATE kvs_glob_module kvs_warnings)
endif()
