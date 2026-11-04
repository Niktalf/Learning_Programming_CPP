cmake_minimum_required(VERSION 3.28)
project(kvs LANGUAGES CXX)

# Этап 1 идёт под C++14: на лекции 1 разбирались auto, лямбды, категории
# значений и правила трёх, пяти и нуля — всё это язык 2011–2014 годов.
# Стандарт поднимается вместе с курсом: на этапах 2–4 будет C++17, с этапа 5 —
# C++23. Так компилятор сам не даст применить то, чего ещё не было на лекциях.
set(CMAKE_CXX_STANDARD 14)
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
    src/value.cpp
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
    tests/test_insert_order.cpp)
target_link_libraries(kvs_tests PRIVATE kvs_core kvs_warnings GTest::gtest_main)

# Звёздочка: порядок вызовов конструкторов при вставке в std::map.
add_executable(kvs_insert_order tools/insert_order.cpp)
target_include_directories(kvs_insert_order PRIVATE tests)
target_link_libraries(kvs_insert_order PRIVATE kvs_warnings)

include(GoogleTest)
gtest_discover_tests(kvs_tests)
