// src/detail.hpp — общее для продюсера и потребителя. Студентам не виден.

#pragma once

#include "lab/kafka/event_bus.hpp"

#include <kafka/KafkaClient.h>
#include <kafka/KafkaException.h>
#include <kafka/Properties.h>
#include <kafka/Types.h>

#include <cstdio>
#include <exception>
#include <string>

namespace lab::kafka::detail {

// Журнал клиента. По умолчанию modern-cpp-kafka печатает служебные строки
// уровня NOTICE в stdout, и они перемешались бы с выводом программы
// студента. Оставляем только предупреждения и ошибки (уровни 0–4) и пишем
// их в stderr с узнаваемым префиксом.
inline void log_to_stderr(int level, const char* /*file*/, int /*line*/, const char* msg) {
    if (level <= 4) {
        std::fprintf(stderr, "[kafka] %s\n", msg);
    }
}

inline void quiet_logging(::kafka::Properties& props) {
    props.put("log_level", "4");
    props.put("log_cb", ::kafka::clients::LogCallback(log_to_stderr));
    // Те же ошибки соединения библиотека повторяет через error_cb.
    // Одного канала достаточно.
    props.put("error_cb", ::kafka::clients::ErrorCallback([](const ::kafka::Error&) {}));
}

// Текст ошибки без отметки времени и пути к заголовку библиотеки,
// которые KafkaException добавляет в what().
inline std::string describe(const std::exception& e) {
    if (const auto* ke = dynamic_cast<const ::kafka::KafkaException*>(&e)) {
        return ke->error().message();
    }
    return e.what();
}

// Содержимое буфера сообщения — байт в байт. Не toString(): тот заменяет
// непечатаемые символы на «[0x..]» и годится только для отладочной печати.
inline std::string bytes(const ::kafka::ConstBuffer& buffer) {
    if (buffer.size() == 0) {
        return {};
    }
    return std::string(static_cast<const char*>(buffer.data()), buffer.size());
}

[[noreturn]] inline void rethrow(const char* where, const std::exception& e) {
    throw Error(std::string(where) + ": " + describe(e));
}

}  // namespace lab::kafka::detail
