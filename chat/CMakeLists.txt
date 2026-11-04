cmake_minimum_required(VERSION 3.28)
project(chat LANGUAGES CXX)

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
add_library(chat_warnings INTERFACE)
target_compile_options(chat_warnings INTERFACE -Wall -Wextra -Wshadow)

include(FetchContent)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2)
FetchContent_MakeAvailable(googletest)

add_library(chat_core
    src/message.cpp
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
    tests/test_push_order.cpp)
target_include_directories(chat_tests PRIVATE tests)
target_link_libraries(chat_tests PRIVATE chat_core chat_warnings GTest::gtest_main)

# Звёздочка: порядок конструкторов при добавлении в std::vector и перевыделении.
add_executable(chat_push_order tools/push_order.cpp)
target_include_directories(chat_push_order PRIVATE tests)
target_link_libraries(chat_push_order PRIVATE chat_warnings)

include(GoogleTest)
gtest_discover_tests(chat_tests)
