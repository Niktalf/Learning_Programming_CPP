cmake_minimum_required(VERSION 3.28)
project(taskq LANGUAGES CXX)

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
# поэтому сканирование выключено везде и включается лишь для его целей.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

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
    src/payload.cpp
    src/task.cpp
    src/log.cpp
    src/task_key.cpp
    src/lexer.cpp
    src/convert.cpp
    src/commands.cpp
    src/service.cpp
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
    tests/test_commands.cpp
    tests/test_lifetime.cpp
    tests/test_push_order.cpp
    tests/test_errors.cpp
    tests/test_state.cpp
    tests/test_validate.cpp
    tests/test_guarantee.cpp
    tests/test_handler.cpp
    tests/test_result.cpp
    tests/test_fuzz.cpp
    tests/test_payload.cpp
    tests/test_workers.cpp
    tests/test_log.cpp
    tests/test_cycle.cpp
    tests/test_concepts.cpp
    tests/test_list.cpp
    tests/test_task_key.cpp
    tests/test_generator.cpp
    tests/test_registry.cpp
    tests/test_convert.cpp
    tests/test_static_registry.cpp
    tests/test_queue_iterator.cpp
    tests/test_emplace.cpp
    tests/test_invalidation.cpp)
target_include_directories(taskq_tests PRIVATE tests tools)
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

# Этап 7: замеры контейнеров. Собираются всегда, запускаются руками и только
# из оптимизированной сборки — цифры отладочной ничего не говорят. Результаты
# и выводы — docs/benchmarks.md.
foreach(bench taskq_bench_queue taskq_bench_index)
    string(REPLACE "taskq_" "" source ${bench})
    add_executable(${bench} bench/${source}.cpp tools/alloc_counter.cpp)
    target_include_directories(${bench} PRIVATE tools bench)
    target_link_libraries(${bench} PRIVATE taskq_warnings)
endforeach()

# Домашнее задание этапа 7: обход очереди с удалением внутри цикла.
# Намеренно неопределённое поведение. Санитайзер адресов его не видит — чтение
# освобождённого узла происходит внутри libstdc++.so (docs/iterators.md); видят
# valgrind и отладочный режим libstdc++. taskq_invalidation_demo — для
# valgrind, в обычную сборку не входит. taskq_invalidation_debug —
# отладочный режим, и тест ctest ждёт от него сообщения о недействительном
# итераторе. С taskq_core программа не связывается: отладочный режим меняет
# устройство контейнеров, и смешивать его с библиотекой без него нельзя.
add_executable(taskq_invalidation_demo EXCLUDE_FROM_ALL tools/invalidation_demo.cpp)
target_include_directories(taskq_invalidation_demo PRIVATE include)
target_link_libraries(taskq_invalidation_demo PRIVATE taskq_warnings)

add_executable(taskq_invalidation_debug tools/invalidation_demo.cpp)
target_include_directories(taskq_invalidation_debug PRIVATE include)
target_compile_definitions(taskq_invalidation_debug PRIVATE _GLIBCXX_DEBUG)
target_link_libraries(taskq_invalidation_debug PRIVATE taskq_warnings)
add_test(NAME taskq_invalidation_detected
         COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:taskq_invalidation_debug>
                 "-DEXPECT=singular iterator"
                 -P ${CMAKE_SOURCE_DIR}/cmake/expect_runtime_failure.cmake)

include(GoogleTest)
gtest_discover_tests(taskq_tests)
gtest_discover_tests(taskq_alloc_tests)

# Этап 3, второй путь: цена исключений и std::expected без ошибок и с
# ошибками. С этапа 6 меряется лексер: разбор команд ушёл в реестр.
add_executable(taskq_error_cost tools/error_cost.cpp)
target_include_directories(taskq_error_cost PRIVATE tools)
target_link_libraries(taskq_error_cost PRIVATE taskq_core taskq_warnings)

# Учебная демонстрация висячего представления. В обычную сборку не входит.
add_executable(taskq_dangling_demo EXCLUDE_FROM_ALL tests/dangling_demo.cpp)
target_link_libraries(taskq_dangling_demo PRIVATE taskq_core taskq_warnings)

# Звёздочка этапа 4: цикл из shared_ptr. taskq_cycle_demo — исправленный
# вариант, задача держит воркера через weak_ptr; входит в обычную сборку.
# taskq_cycle_leak — тот же код под флагом TASKQ_LEAKING_CYCLE, задача держит
# воркера через shared_ptr. В обычную сборку не входит: программа намеренно
# теряет память. Запускать под санитайзером адресов, он покажет утечку.
add_executable(taskq_cycle_demo tools/cycle_demo.cpp)
target_include_directories(taskq_cycle_demo PRIVATE tools)
target_link_libraries(taskq_cycle_demo PRIVATE taskq_warnings)

add_executable(taskq_cycle_leak EXCLUDE_FROM_ALL tools/cycle_demo.cpp)
target_include_directories(taskq_cycle_leak PRIVATE tools)
target_compile_definitions(taskq_cycle_leak PRIVATE TASKQ_LEAKING_CYCLE)
target_link_libraries(taskq_cycle_leak PRIVATE taskq_warnings)

# --- Этап 5: программы, которые не должны собираться -----------------------
#
# Каждая — отдельная цель вне обычной сборки и тест, который пытается её
# собрать и проверяет, что сборка упала, причём по ожидаемой причине: вывод
# компилятора сверяется с образцом. Так «не компилируется» становится
# проверяемым утверждением, а не словами в README.
function(taskq_expect_compile_failure name source expect)
    add_executable(${name} EXCLUDE_FROM_ALL ${source})
    target_link_libraries(${name} PRIVATE taskq_core taskq_warnings)
    add_test(NAME ${name}
             COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -DTARGET=${name}
                     "-DEXPECT=${expect}"
                     -P ${CMAKE_SOURCE_DIR}/cmake/expect_compile_failure.cmake)
endfunction()

# Конвейер над временным контейнером: ranges::find возвращает
# std::ranges::dangling вместо итератора.
taskq_expect_compile_failure(taskq_dangling_range_fail tests/compile_fail/dangling_range.cpp
                             "dangling")

# Одна и та же ошибка — имя очереди числом — с концепцией и с enable_if.
# Сравнение диагностики — в docs/concepts.md.
taskq_expect_compile_failure(taskq_concept_diagnostics_fail tests/compile_fail/concept_name.cpp
                             "NameLike")
taskq_expect_compile_failure(taskq_enable_if_diagnostics_fail tests/compile_fail/enable_if_name.cpp
                             "enable_if")

# Этап 6: обработчик с параметром, который convert не умеет, — концепция
# CommandHandler отвергает его в строке регистрации.
taskq_expect_compile_failure(taskq_bad_handler_fail tests/compile_fail/bad_handler.cpp
                             "ArgumentsSupported")

# Звёздочка этапа 6: повтор имени в StaticRegistry — static_assert.
taskq_expect_compile_failure(taskq_duplicate_command_fail tests/compile_fail/duplicate_command.cpp
                             "duplicate command name")

# Домашнее задание этапа 5: модули. Включаются явно, -DTASKQ_MODULES=ON:
# поддержка в CMake, Ninja и компиляторах всё ещё неровная, и обычная сборка
# от неё зависеть не должна. Что получилось и что нет — docs/modules.md.
option(TASKQ_MODULES "Build the C++ modules experiment (stage 5 homework)" OFF)
if(TASKQ_MODULES)
    add_library(taskq_state_module)
    set_target_properties(taskq_state_module PROPERTIES CXX_SCAN_FOR_MODULES ON)
    target_sources(taskq_state_module PUBLIC FILE_SET CXX_MODULES FILES modules/taskq.state.cppm)
    target_link_libraries(taskq_state_module PRIVATE taskq_warnings)
    add_executable(taskq_modules_demo modules/demo.cpp)
    set_target_properties(taskq_modules_demo PROPERTIES CXX_SCAN_FOR_MODULES ON)
    target_link_libraries(taskq_modules_demo PRIVATE taskq_state_module taskq_warnings)
endif()
