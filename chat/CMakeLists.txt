cmake_minimum_required(VERSION 3.28)
project(chat LANGUAGES CXX)

# С этапа 5 — C++23. На лекции 5 разбирались концепции, ranges, оператор <=>
# и корутины; вместе с двадцать третьим стандартом приходят std::expected
# (он заменяет собственный Result этапа 3), std::print, std::generator,
# views::enumerate и ranges::to.
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
    src/convert.cpp
    src/commands.cpp
    src/errors.cpp
    src/server.cpp
    src/log.cpp
    src/validate.cpp
    src/message_id.cpp
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
    tests/test_commands.cpp
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
    tests/test_cycle.cpp
    tests/test_concepts.cpp
    tests/test_history.cpp
    tests/test_message_id.cpp
    tests/test_generator.cpp
    tests/test_registry.cpp
    tests/test_convert.cpp
    tests/test_static_registry.cpp)
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

# Этап 3, второй путь: цена исключений и std::expected без ошибок и с
# ошибками. С этапа 6 меряется лексер: разбор команд ушёл в реестр.
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

# --- Этап 5: программы, которые не должны собираться -----------------------
#
# Каждая — отдельная цель вне обычной сборки и тест, который пытается её
# собрать и проверяет, что сборка упала, причём по ожидаемой причине: вывод
# компилятора сверяется с образцом. Так «не компилируется» становится
# проверяемым утверждением, а не словами в README.
function(chat_expect_compile_failure name source expect)
    add_executable(${name} EXCLUDE_FROM_ALL ${source})
    target_link_libraries(${name} PRIVATE chat_core chat_warnings)
    add_test(NAME ${name}
             COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -DTARGET=${name}
                     "-DEXPECT=${expect}"
                     -P ${CMAKE_SOURCE_DIR}/cmake/expect_compile_failure.cmake)
endfunction()

# Конвейер над временным контейнером: ranges::find возвращает
# std::ranges::dangling вместо итератора.
chat_expect_compile_failure(chat_dangling_range_fail tests/compile_fail/dangling_range.cpp
                            "dangling")

# Одна и та же ошибка — имя комнаты числом — с концепцией и с enable_if.
# Сравнение диагностики — в docs/concepts.md.
chat_expect_compile_failure(chat_concept_diagnostics_fail tests/compile_fail/concept_name.cpp
                            "NameLike")
chat_expect_compile_failure(chat_enable_if_diagnostics_fail tests/compile_fail/enable_if_name.cpp
                            "enable_if")

# Этап 6: обработчик с параметром, который convert не умеет, — концепция
# CommandHandler отвергает его в строке регистрации.
chat_expect_compile_failure(chat_bad_handler_fail tests/compile_fail/bad_handler.cpp
                            "ArgumentsSupported")

# Звёздочка этапа 6: повтор имени в StaticRegistry — static_assert.
chat_expect_compile_failure(chat_duplicate_command_fail tests/compile_fail/duplicate_command.cpp
                            "duplicate command name")

# Домашнее задание этапа 5: модули. Включаются явно, -DCHAT_MODULES=ON:
# поддержка в CMake, Ninja и компиляторах всё ещё неровная, и обычная сборка
# от неё зависеть не должна. Что получилось и что нет — docs/modules.md.
option(CHAT_MODULES "Build the C++ modules experiment (stage 5 homework)" OFF)
if(CHAT_MODULES)
    add_library(chat_utf8_module)
    set_target_properties(chat_utf8_module PROPERTIES CXX_SCAN_FOR_MODULES ON)
    target_sources(chat_utf8_module PUBLIC FILE_SET CXX_MODULES FILES modules/chat.utf8.cppm)
    target_link_libraries(chat_utf8_module PRIVATE chat_warnings)
    add_executable(chat_modules_demo modules/demo.cpp)
    set_target_properties(chat_modules_demo PROPERTIES CXX_SCAN_FOR_MODULES ON)
    target_link_libraries(chat_modules_demo PRIVATE chat_utf8_module chat_warnings)
endif()
