// Второй пункт сравнения из docs/errors.md: во что обходятся два пути
// обработки ошибок, когда ошибок нет, и когда ошибка — каждая строка.
//
// Утилита разбирает одни и те же строки через tokenize (исключения) и
// try_tokenize (ошибка-значение) и печатает время на строку. До этапа 6 здесь
// сравнивались parse и try_parse; с реестром команд разбор глагола ушёл в
// реестр, а два пути обработки ошибок остались в лексере — его и меряем.
//
// Цифры зависят от машины и флагов сборки, поэтому смотреть надо на
// соотношения, а не на абсолютные значения, и только в оптимизированной
// сборке (RelWithDebInfo или Release):
//
//   - корректный ввод: оба пути стоят примерно одинаково. Исключение, которое
//     не бросили, по времени бесплатно — современные компиляторы кладут
//     таблицы раскрутки отдельно от кода (модель «zero-cost»);
//   - некорректный ввод: бросок исключения дороже возврата примерно на
//     порядок — выделение объекта исключения, поиск обработчика по таблицам,
//     раскрутка стека. Возврат ошибки тоже не бесплатен: ParseError несёт две
//     строки, и их приходится собрать в обоих путях. Поэтому исключения —
//     для исключительного, а не для ожидаемого: на этапе 15 сетевой ввод от
//     злонамеренного клиента может весь состоять из мусора, и там разумнее
//     отказ разбора вернуть значением.
//
// Цифры на машине, где писался эталон, — в docs/errors.md.

#include <chrono>
#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "taskq/errors.hpp"
#include "taskq/lexer.hpp"
#include "workload.hpp"

namespace {

using Clock = std::chrono::steady_clock;

// Время на одну строку в наносекундах. Счётчик успехов нужен, чтобы
// оптимизатор не выбросил разбор как не имеющий наблюдаемого эффекта.
template <class Parse>
double nanoseconds_per_line(const std::vector<std::string>& lines, int rounds, Parse parse_one,
                            std::size_t& successes) {
    const auto start = Clock::now();
    for (int round = 0; round < rounds; ++round) {
        for (const std::string& line : lines) {
            successes += parse_one(line) ? 1 : 0;
        }
    }
    const std::chrono::duration<double, std::nano> elapsed = Clock::now() - start;
    return elapsed.count() / static_cast<double>(lines.size() * static_cast<std::size_t>(rounds));
}

bool with_exceptions(std::string_view line) {
    try {
        static_cast<void>(taskq::tokenize(line));
        return true;
    } catch (const taskq::ParseError&) {
        return false;
    }
}

bool with_result(std::string_view line) {
    return taskq::try_tokenize(line).has_value();
}

void report(const char* title, const std::vector<std::string>& texts, int rounds) {
    std::size_t successes = 0;
    const double thrown = nanoseconds_per_line(texts, rounds, with_exceptions, successes);
    const double returned = nanoseconds_per_line(texts, rounds, with_result, successes);
    std::cout << title << '\n'
              << "  exceptions: " << thrown << " ns per line\n"
              << "  expected:   " << returned << " ns per line\n"
              << "  ratio:      " << thrown / returned << "\n"
              << "  (parsed " << successes << " lines in total)\n";
}

}  // namespace

int main() {
    constexpr int rounds = 200;
    report("valid input, no errors:", taskq::tools::sample_commands(), rounds);

    const std::vector<std::string> garbage(1000, "FROB build \"unterminated");
    report("invalid input, every line fails:", garbage, rounds);
    return 0;
}
