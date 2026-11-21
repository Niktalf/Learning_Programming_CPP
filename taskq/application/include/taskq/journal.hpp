#pragma once

#include <string_view>

namespace taskq {

// Этап 9: журнал переходов — порт, а не файл. Потребитель — сервис, и
// интерфейс объявлен здесь, рядом с ним; файловая реализация — AppendOnlyLog
// в log.hpp, и сервис её не включает.
//
// До этапа 9 сервис держал std::optional<AppendOnlyLog> и сам открывал файл
// по пути (DIP, docs/audit.md, запись 4). Тест, который хотел увидеть, какие
// переходы записаны, должен был писать файл. Теперь журнал — узкий
// интерфейс, и тесты подставляют журнал в памяти.
//
// Журнал переходов — запись «номер<TAB>из<TAB>в»; восстановления по нему
// пока нет (снапшот — этап 17), поэтому порт один.
class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void append(std::string_view record) = 0;

protected:
    ILogSink() = default;
    ILogSink(const ILogSink&) = default;
    ILogSink& operator=(const ILogSink&) = default;
};

}  // namespace taskq
