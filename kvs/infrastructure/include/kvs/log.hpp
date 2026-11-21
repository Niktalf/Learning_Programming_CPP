#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kvs/journal.hpp"

namespace kvs {

// Журнал операций — первый в проекте класс над внешним ресурсом.
//
// RAII: файл открывается в конструкторе и закрывается в деструкторе. В
// публичном интерфейсе нет ни open, ни close — время жизни ресурса совпадает
// со временем жизни объекта, и забыть закрыть файл невозможно. Это работает
// и при выходе через исключение: раскрутка стека вызовет деструктор. Тест
// Log.ClosedOnException доказывает это подсчётом открытых дескрипторов.
//
// Если файл не открылся, конструктор бросает. Деструктор при этом не
// вызовется — объект не был построен. Но поле out_, уже сконструированное к
// моменту броска, разрушится корректно: именно поэтому ресурс держится в
// поле-объекте, а не в сыром дескрипторе.
//
// На этапе 15 по тому же образцу будут жить сокеты и сессии.
//
// Этап 9: файловая реализация порта ILogSink (journal.hpp). Сервер о ней
// не знает; создаёт её точка сборки (infrastructure/src/compose.cpp).
class AppendOnlyLog final : public ILogSink {
public:
    explicit AppendOnlyLog(const std::filesystem::path& path);

    // Копирование запрещено: два объекта, пишущие в один файл, — это два
    // владельца одного ресурса. Перемещение разрешено: владелец меняется.
    AppendOnlyLog(const AppendOnlyLog&) = delete;
    AppendOnlyLog& operator=(const AppendOnlyLog&) = delete;
    AppendOnlyLog(AppendOnlyLog&&) noexcept = default;
    AppendOnlyLog& operator=(AppendOnlyLog&&) noexcept = default;
    ~AppendOnlyLog() override = default;

    // Запись одной строки. Сбрасывается на диск сразу: журнал, теряющий
    // хвост при падении процесса, бесполезен. Ошибка записи — KvError.
    void append(std::string_view record) override;

private:
    std::ofstream out_;
};

// Этап 9: файловая реализация порта ILogSource — записи журнала для
// восстановления. Файла нет — записей нет: первый запуск с --log — не
// ошибка. До этапа 9 эта проверка жила в конструкторе сервера.
class FileHistory final : public ILogSource {
public:
    explicit FileHistory(std::filesystem::path path) : path_(std::move(path)) {}

    [[nodiscard]] std::vector<std::string> records() const override;
    [[nodiscard]] std::string name() const override { return path_.string(); }

private:
    std::filesystem::path path_;
};

}  // namespace kvs
