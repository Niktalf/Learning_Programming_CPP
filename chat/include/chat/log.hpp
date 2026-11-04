#pragma once

#include <filesystem>
#include <fstream>
#include <string_view>

namespace chat {

// Журнал сообщений — первый в проекте класс над внешним ресурсом.
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
class AppendOnlyLog {
public:
    explicit AppendOnlyLog(const std::filesystem::path& path);

    // Копирование запрещено: два объекта, пишущие в один файл, — это два
    // владельца одного ресурса. Перемещение разрешено: владелец меняется.
    AppendOnlyLog(const AppendOnlyLog&) = delete;
    AppendOnlyLog& operator=(const AppendOnlyLog&) = delete;
    AppendOnlyLog(AppendOnlyLog&&) noexcept = default;
    AppendOnlyLog& operator=(AppendOnlyLog&&) noexcept = default;
    ~AppendOnlyLog() = default;

    // Запись одной строки. Сбрасывается на диск сразу: журнал, теряющий
    // хвост при падении процесса, бесполезен. Ошибка записи — ChatError.
    void append(std::string_view record);

private:
    std::ofstream out_;
};

}  // namespace chat
