# Этап 10: в перечисленных каталогах не должно быть ни одного упоминания
# слова WORD — ни в коде, ни в комментариях, без учёта регистра.
#
#   cmake -DROOT=<исходники> -DDIRS="domain;application" -DWORD=kafka -P expect_no_mention.cmake
#
# Это та же проверка, что grep -ri kafka domain application на занятии, но
# в ctest: ядро, которое знает имя брокера, — уже протёкший слой, даже если
# пока это только комментарий.
string(TOLOWER "${WORD}" word)
set(found "")
foreach(dir IN LISTS DIRS)
    file(GLOB_RECURSE sources "${ROOT}/${dir}/*")
    foreach(source IN LISTS sources)
        file(STRINGS "${source}" lines)
        set(number 0)
        foreach(line IN LISTS lines)
            math(EXPR number "${number} + 1")
            string(TOLOWER "${line}" lower)
            string(FIND "${lower}" "${word}" position)
            if(NOT position EQUAL -1)
                file(RELATIVE_PATH relative "${ROOT}" "${source}")
                string(APPEND found "\n  ${relative}:${number}: ${line}")
            endif()
        endforeach()
    endforeach()
endforeach()
if(found)
    message(FATAL_ERROR "'${WORD}' mentioned in ${DIRS}:${found}")
endif()
message(STATUS "no '${WORD}' in ${DIRS}")
