cmake_minimum_required(VERSION 3.28)
project(kvs LANGUAGES CXX)

# Этап 10: слои — kvs_domain, kvs_application, kvs_infrastructure — и
# публикация доменных событий в Kafka (docs/layers.md, docs/events.md).
#
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

# --- Этап 10: слои ----------------------------------------------------------
#
# Три библиотеки, зависимости только внутрь (docs/layers.md):
#
#   kvs_infrastructure ──► kvs_application ──► kvs_domain
#        │
#        └── PRIVATE lab::kafka
#
# У каждого слоя свой каталог заголовков, и цель видит только свой и те, что
# под ним: домену заголовки сервера и движков просто не найти. Пути
# включения при этом прежние — "kvs/store.hpp", "kvs/server.hpp", — каталог
# слоя добавляется к поиску, а не к имени.
#
# Домен не линкуется ни с чем, кроме стандартной библиотеки; что он
# действительно собирается и работает один, проверяет kvs_domain_isolated.
add_library(kvs_domain
    domain/src/ivalue.cpp
    domain/src/value.cpp
    domain/src/errors.cpp
    domain/src/glob.cpp
    domain/src/bytes.cpp
    domain/src/owning_buffer.cpp)
target_include_directories(kvs_domain PUBLIC domain/include)
target_link_libraries(kvs_domain PRIVATE kvs_warnings)

# Прикладной слой: команды, их разбор, сервер с границей ошибок, порты
# журнала. Знает домен, не знает ни файлов, ни брокера.
add_library(kvs_application
    application/src/request_cache.cpp
    application/src/server.cpp
    application/src/lexer.cpp
    application/src/commands.cpp
    application/src/reply.cpp)
target_include_directories(kvs_application PUBLIC application/include)
target_link_libraries(kvs_application PUBLIC kvs_domain PRIVATE kvs_warnings)

# Инфраструктура: движки, файловый журнал, формат событий, адаптер Kafka и
# точка сборки. Обёртка брокера — PRIVATE: её заголовок не виден ни одной
# цели, которая линкуется с инфраструктурой.
#
# KVS_KAFKA=OFF собирает проект без обёртки — например, на машине, где она
# ещё не установлена. Сервер тогда работает без событий, --events отвергается.
option(KVS_KAFKA "Build the Kafka event publisher and kvs_tail (needs lab_kafka)" ON)
add_library(kvs_infrastructure
    infrastructure/src/log.cpp
    infrastructure/src/file.cpp
    infrastructure/src/engines.cpp
    infrastructure/src/event_wire.cpp
    infrastructure/src/compose.cpp)
target_include_directories(kvs_infrastructure PUBLIC infrastructure/include)
target_link_libraries(kvs_infrastructure PUBLIC kvs_application PRIVATE kvs_warnings)
if(KVS_KAFKA)
    find_package(lab_kafka QUIET)
    if(NOT lab_kafka_FOUND)
        message(FATAL_ERROR "lab_kafka not found. Install it (lab-kafka/install.sh) "
                            "or configure with -DKVS_KAFKA=OFF to build without events.")
    endif()
    target_sources(kvs_infrastructure PRIVATE infrastructure/src/kafka_publisher.cpp)
    target_link_libraries(kvs_infrastructure PRIVATE lab::kafka)
    target_compile_definitions(kvs_infrastructure PRIVATE KVS_WITH_KAFKA)
endif()

# kvs_core — все три слоя одним именем, для тестов и утилит прошлых этапов,
# которым нужна вся система. Своих исходников у неё нет.
add_library(kvs_core INTERFACE)
target_link_libraries(kvs_core INTERFACE kvs_infrastructure)

# Приложения — четвёртый, внешний круг: консольный клиент и подписчик.
add_executable(kvs_cli app/main.cpp)
target_link_libraries(kvs_cli PRIVATE kvs_infrastructure kvs_warnings)

if(KVS_KAFKA)
    add_executable(kvs_tail app/tail.cpp)
    target_link_libraries(kvs_tail PRIVATE kvs_infrastructure lab::kafka kvs_warnings)
endif()

enable_testing()
add_executable(kvs_tests
    tests/test_value.cpp
    tests/test_owning_buffer.cpp
    tests/test_store.cpp
    tests/test_insert_order.cpp
    tests/test_lexer.cpp
    tests/test_commands.cpp
    tests/test_lifetime.cpp
    tests/test_errors.cpp
    tests/test_guarantee.cpp
    tests/test_server.cpp
    tests/test_result.cpp
    tests/test_fuzz.cpp
    tests/test_hierarchy.cpp
    tests/test_cache.cpp
    tests/test_cycle.cpp
    tests/test_concepts.cpp
    tests/test_keys.cpp
    tests/test_version.cpp
    tests/test_generator.cpp
    tests/test_registry.cpp
    tests/test_convert.cpp
    tests/test_static_registry.cpp
    tests/test_lists.cpp
    tests/test_store_iterator.cpp
    tests/test_emplace.cpp
    tests/test_invalidation.cpp
    tests/test_hash_tables.cpp
    tests/test_engines.cpp
    tests/test_scan.cpp
    tests/test_clock.cpp
    tests/test_events.cpp
    tests/test_event_wire.cpp)
target_include_directories(kvs_tests PRIVATE tools tests)
target_link_libraries(kvs_tests PRIVATE kvs_core kvs_warnings GTest::gtest_main)

# Этап 9: тесты, которым нужна файловая система, — отдельно. Модульные
# тесты (kvs_tests) не пишут файлов и не смотрят на часы: журнал и часы —
# порты с подставными реализациями. Здесь — файловые адаптеры и точка
# сборки с настоящими файлами; метка integration отделяет их в ctest
# (ctest -LE integration — только модульные).
add_executable(kvs_integration_tests
    tests/test_log.cpp
    tests/test_file.cpp
    tests/test_compose.cpp)
target_include_directories(kvs_integration_tests PRIVATE tools tests)
target_link_libraries(kvs_integration_tests PRIVATE kvs_core kvs_warnings GTest::gtest_main)

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

# Этап 3, второй путь: цена исключений и std::expected без ошибок и с
# ошибками. С этапа 6 меряется лексер: разбор команд ушёл в реестр.
add_executable(kvs_error_cost tools/error_cost.cpp)
target_include_directories(kvs_error_cost PRIVATE tools)
target_link_libraries(kvs_error_cost PRIVATE kvs_core kvs_warnings)

# Этап 7: замеры контейнеров. Собираются всегда, запускаются руками и только
# из оптимизированной сборки — цифры отладочной ничего не говорят. Результаты
# и выводы — docs/benchmarks.md.
foreach(bench kvs_bench_lists kvs_bench_tables kvs_bench_engines kvs_bench_open kvs_hash_histogram)
    string(REPLACE "kvs_" "" source ${bench})
    add_executable(${bench} bench/${source}.cpp tools/alloc_counter.cpp)
    target_include_directories(${bench} PRIVATE tools bench domain/include infrastructure/include)
    target_link_libraries(${bench} PRIVATE kvs_warnings)
endforeach()

# Домашнее задание этапа 7: обход с удалением внутри цикла.
# kvs_invalidation_demo — намеренно неопределённое поведение, в обычную
# сборку не входит; запускать под санитайзером адресов (случай map).
# kvs_invalidation_debug — та же программа в отладочном режиме libstdc++,
# который проверяет каждый итератор; тест ctest ждёт от него сообщения о
# недействительном итераторе (случай deque, санитайзеру невидимый).
add_executable(kvs_invalidation_demo EXCLUDE_FROM_ALL tools/invalidation_demo.cpp)
target_link_libraries(kvs_invalidation_demo PRIVATE kvs_warnings)

add_executable(kvs_invalidation_debug tools/invalidation_demo.cpp)
target_compile_definitions(kvs_invalidation_debug PRIVATE _GLIBCXX_DEBUG)
target_link_libraries(kvs_invalidation_debug PRIVATE kvs_warnings)
add_test(NAME kvs_invalidation_detected
         COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:kvs_invalidation_debug> -DARGS=deque
                 "-DEXPECT=singular iterator"
                 -P ${CMAKE_SOURCE_DIR}/cmake/expect_runtime_failure.cmake)

# --- Этап 10: проверки слоёв ------------------------------------------------
#
# Домен собирается и работает один: программа линкуется только с kvs_domain
# и подставляет свой движок. Протечка зависимости наружу — ошибка сборки
# или линковки этой цели.
add_executable(kvs_domain_isolated tests/isolation/domain_only.cpp)
target_link_libraries(kvs_domain_isolated PRIVATE kvs_domain kvs_warnings)
add_test(NAME kvs_domain_isolated COMMAND kvs_domain_isolated)

# Ни слова о брокере в ядре — даже в комментариях.
add_test(NAME kvs_core_knows_no_broker
         COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} "-DDIRS=domain;application" -DWORD=kafka
                 -P ${CMAKE_SOURCE_DIR}/cmake/expect_no_mention.cmake)

# Сквозной тест с брокером — на макете кластера из librdkafka, который
# поднимается внутри процесса: настоящий протокол, но без Docker. Цель
# собирается, только если в системе есть rdkafka_mock.h. Модульные тесты
# (kvs_tests) брокера не требуют вовсе и проходят при остановленном стенде.
if(KVS_KAFKA)
    find_path(KVS_RDKAFKA_MOCK_DIR librdkafka/rdkafka_mock.h HINTS ${RDKAFKA_INCLUDE_DIRS})
    if(KVS_RDKAFKA_MOCK_DIR)
        add_executable(kvs_kafka_tests tests/test_kafka.cpp)
        target_include_directories(kvs_kafka_tests PRIVATE tests ${KVS_RDKAFKA_MOCK_DIR})
        target_link_libraries(kvs_kafka_tests PRIVATE kvs_core lab::kafka PkgConfig::RDKAFKA kvs_warnings
                                                      GTest::gtest_main)
    else()
        message(STATUS "rdkafka_mock.h not found: kvs_kafka_tests is not built")
    endif()
endif()

include(GoogleTest)
gtest_discover_tests(kvs_tests)
gtest_discover_tests(kvs_alloc_tests)
gtest_discover_tests(kvs_integration_tests PROPERTIES LABELS integration)
if(TARGET kvs_kafka_tests)
    gtest_discover_tests(kvs_kafka_tests PROPERTIES LABELS "integration\\;kafka" TIMEOUT 120
                         DISCOVERY_TIMEOUT 30)
endif()

# Этап 8: тот же набор тестов на каждом движке хранилища, без единой правки
# в тестах. Движок по умолчанию берётся из переменной окружения KVS_ENGINE
# (engine_kind.hpp), и ctest запускает набор ещё трижды — на собственной
# хеш-таблице, на ней же с заведомо плохой хеш-функцией и на открытой
# адресации. Имена тестов получают приставку движка: hash/Store.SetGetDelete.
foreach(engine hash bad-hash open)
    gtest_discover_tests(kvs_tests TEST_PREFIX "${engine}/"
                         PROPERTIES ENVIRONMENT "KVS_ENGINE=${engine}")
endforeach()

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

# Этап 6: обработчик с параметром, который convert не умеет, — концепция
# CommandHandler отвергает его в строке регистрации.
kvs_expect_compile_failure(kvs_bad_handler_fail tests/compile_fail/bad_handler.cpp
                           "ArgumentsSupported")

# Звёздочка этапа 6: повтор имени в StaticRegistry — static_assert.
kvs_expect_compile_failure(kvs_duplicate_command_fail tests/compile_fail/duplicate_command.cpp
                           "duplicate command name")

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
