# Запуск программы, которая обязана упасть, — и упасть по нужной причине.
# Аварийное завершение (abort от отладочного режима libstdc++) ctest считает
# провалом даже с PASS_REGULAR_EXPRESSION, поэтому программа запускается
# отсюда, а вывод сверяется с ожидаемым текстом.
#
#   cmake -DPROGRAM=... -DARGS=... -DEXPECT=regex -P expect_runtime_failure.cmake
execute_process(
    COMMAND ${PROGRAM} ${ARGS}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
if(result EQUAL 0)
    message(FATAL_ERROR "${PROGRAM} ${ARGS} finished normally, but it must fail")
endif()
if(NOT output MATCHES "${EXPECT}")
    message(FATAL_ERROR "${PROGRAM} failed for a different reason; expected /${EXPECT}/:\n${output}")
endif()
message(STATUS "${PROGRAM} ${ARGS} failed as expected")
