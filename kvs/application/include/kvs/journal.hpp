#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace kvs {

// Этап 9: журнал — порты сервера, а не файл. Сервер — потребитель, и
// интерфейсы объявлены здесь, рядом с ним; файловая реализация — в
// file_log.hpp, и сервер её не включает.
//
// До этапа 9 сервер сам открывал файл по пути из Options, сам проверял, есть
// ли он, сам читал строки для восстановления и сам держал AppendOnlyLog
// (DIP, docs/audit.md, запись 4). Его тесты поэтому писали во временный
// каталог. Теперь журнал — два узких интерфейса: куда писать и откуда
// восстанавливаться, — и тесты подставляют журнал в памяти.

// Куда пишутся успешные изменения: одна строка — одна команда.
class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void append(std::string_view record) = 0;

protected:
    ILogSink() = default;
    ILogSink(const ILogSink&) = default;
    ILogSink& operator=(const ILogSink&) = default;
};

// Откуда при запуске берутся записи для восстановления.
class ILogSource {
public:
    virtual ~ILogSource() = default;
    [[nodiscard]] virtual std::vector<std::string> records() const = 0;
    // Имя источника — для сообщения «while replaying <имя> line N».
    [[nodiscard]] virtual std::string name() const = 0;

protected:
    ILogSource() = default;
    ILogSource(const ILogSource&) = default;
    ILogSource& operator=(const ILogSource&) = default;
};

}  // namespace kvs
