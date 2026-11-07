# Сборка цели, которая обязана не собраться, — и не собраться по нужной
# причине. Просто «сборка упала» доказывает мало: опечатка в тесте тоже
# роняет сборку. Поэтому вывод компилятора сверяется с ожидаемым текстом.
#
#   cmake -DBUILD_DIR=... -DTARGET=... -DEXPECT=regex -P expect_compile_failure.cmake
execute_process(
    COMMAND ${CMAKE_COMMAND} --build ${BUILD_DIR} --target ${TARGET}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
if(result EQUAL 0)
    message(FATAL_ERROR "${TARGET} compiled, but it must not")
endif()
if(NOT output MATCHES "${EXPECT}")
    message(FATAL_ERROR "${TARGET} failed for a different reason; expected /${EXPECT}/:\n${output}")
endif()
message(STATUS "${TARGET} failed to compile as expected")
