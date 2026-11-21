#pragma once

#include <string_view>

namespace chat {

// Этап 9: журнал — порт, а не файл. Потребитель — команда /msg (через
// Context), и интерфейс объявлен здесь, рядом с ней; файловая реализация —
// AppendOnlyLog в log.hpp, и команды её не включают.
//
// До этапа 9 Context держал AppendOnlyLog* — конкретный класс над файлом, —
// а сервер сам открывал его по пути (DIP, docs/audit.md, запись 4). Тест,
// который хотел увидеть, что записано в журнал, должен был писать файл.
// Теперь журнал — узкий интерфейс, и тесты подставляют журнал в памяти.
//
// Журнал чата — архив, как логи IRC-канала: в него только пишут, история
// при перезапуске из него не восстанавливается, — поэтому порт один.
class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void append(std::string_view record) = 0;

protected:
    ILogSink() = default;
    ILogSink(const ILogSink&) = default;
    ILogSink& operator=(const ILogSink&) = default;
};

}  // namespace chat
