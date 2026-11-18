cmake_minimum_required(VERSION 3.28)
project(lab_kafka VERSION 2.0.0 LANGUAGES CXX)

# Обёртка подключается к проектам студентов, которые с этапа 5 собираются
# в C++20, поэтому и сама она не требует ничего новее.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Зависимости --------------------------------------------------------
# librdkafka ставится системным пакетом librdkafka-dev. modern-cpp-kafka
# работает поверх её C-интерфейса, так что C++-библиотека rdkafka++ не нужна.
find_package(PkgConfig REQUIRED)
pkg_check_modules(RDKAFKA REQUIRED IMPORTED_TARGET rdkafka)

# modern-cpp-kafka состоит только из заголовков. Нужная версия (v2024.07.03)
# лежит прямо в архиве, в third_party/: так обёртка не зависит от того, какая
# версия оказалась в /opt образа, — в разных выпусках библиотеки классы лежат
# в разных пространствах имён, и чужая версия может не собраться.
set(MODERN_CPP_KAFKA_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/modern-cpp-kafka/include"
    CACHE PATH "Каталог заголовков modern-cpp-kafka")

if(NOT EXISTS "${MODERN_CPP_KAFKA_INCLUDE}/kafka/KafkaProducer.h")
    message(FATAL_ERROR
        "Не найден modern-cpp-kafka. Ожидался заголовок\n"
        "  ${MODERN_CPP_KAFKA_INCLUDE}/kafka/KafkaProducer.h\n"
        "Архив распакован не полностью?")
endif()

# --- Библиотека ---------------------------------------------------------
add_library(lab_kafka
    src/producer.cpp
    src/consumer.cpp)
add_library(lab::kafka ALIAS lab_kafka)
# Под этим именем цель видна после find_package: lab::kafka, а не
# lab::lab_kafka, как получилось бы по умолчанию.
set_target_properties(lab_kafka PROPERTIES EXPORT_NAME kafka)

target_include_directories(lab_kafka
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

# SYSTEM здесь принципиален: без него предупреждения из заголовков сторонних
# библиотек посыплются в сборку студента, у которого включены -Wall -Wextra,
# и он начнёт отключать предупреждения вместо того, чтобы их исправлять.
target_include_directories(lab_kafka SYSTEM PRIVATE "${MODERN_CPP_KAFKA_INCLUDE}")

target_link_libraries(lab_kafka PUBLIC PkgConfig::RDKAFKA)
target_compile_features(lab_kafka PUBLIC cxx_std_20)
target_compile_options(lab_kafka PRIVATE -Wall -Wextra -Wshadow)

# --- Утилита проверки стенда -------------------------------------------
add_executable(lab_kafka_probe tools/lab_kafka_probe.cpp)
target_link_libraries(lab_kafka_probe PRIVATE lab_kafka)
target_compile_options(lab_kafka_probe PRIVATE -Wall -Wextra -Wshadow)

# --- Самопроверка без стенда ---------------------------------------------
# librdkafka содержит макет кластера Kafka, который говорит по настоящему
# протоколу через TCP. На нём обёртка проверяется без Docker и брокера.
find_path(LAB_KAFKA_MOCK_DIR librdkafka/rdkafka_mock.h
          HINTS ${RDKAFKA_INCLUDEDIR} ${RDKAFKA_INCLUDE_DIRS})
if(LAB_KAFKA_MOCK_DIR)
    enable_testing()
    add_executable(lab_kafka_selftest tests/selftest.cpp)
    target_link_libraries(lab_kafka_selftest PRIVATE lab_kafka)
    target_compile_options(lab_kafka_selftest PRIVATE -Wall -Wextra -Wshadow)
    add_test(NAME lab_kafka_selftest COMMAND lab_kafka_selftest)
    set_tests_properties(lab_kafka_selftest PROPERTIES TIMEOUT 300)
else()
    message(STATUS "rdkafka_mock.h не найден — самопроверка не собирается")
endif()

# --- Заготовки адаптера: только проверка компилируемости ------------------
option(LAB_KAFKA_CHECK_EXAMPLES "Компилировать заготовки адаптера" OFF)
if(LAB_KAFKA_CHECK_EXAMPLES)
    foreach(v A_KVS B_CHAT C_TASKQ)
        add_library(example_${v} OBJECT examples/kafka_event_publisher_${v}.cpp)
        target_link_libraries(example_${v} PRIVATE lab_kafka)
        target_compile_options(example_${v} PRIVATE -Wall -Wextra -Wshadow)
    endforeach()
endif()

# --- Установка ----------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS lab_kafka EXPORT lab_kafka-targets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS lab_kafka_probe RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY examples/ DESTINATION ${CMAKE_INSTALL_DATADIR}/lab_kafka/examples)
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/lab_kafka)

install(EXPORT lab_kafka-targets
        FILE lab_kafka-targets.cmake
        NAMESPACE lab::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lab_kafka)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/lab_kafka-config.cmake.in"
"@PACKAGE_INIT@\n"
"include(CMakeFindDependencyMacro)\n"
"find_dependency(PkgConfig)\n"
"pkg_check_modules(RDKAFKA REQUIRED IMPORTED_TARGET rdkafka)\n"
"include(\"\${CMAKE_CURRENT_LIST_DIR}/lab_kafka-targets.cmake\")\n")

configure_package_config_file(
    "${CMAKE_CURRENT_BINARY_DIR}/lab_kafka-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/lab_kafka-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lab_kafka)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/lab_kafka-config-version.cmake"
    COMPATIBILITY SameMajorVersion)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/lab_kafka-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/lab_kafka-config-version.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lab_kafka)
