// Второй пункт сравнения из docs/errors.md: во что обходятся два пути
// обработки ошибок, когда ошибок нет, и когда ошибка — каждая строка.
//
// Утилита разбирает одни и те же строки через parse (исключения) и try_parse
// (ошибка-значение) и печатает время на строку. Цифры зависят от машины и
// флагов сборки, поэтому смотреть надо на соотношения, а не на абсолютные
// значения, и только в оптимизированной сборке (RelWithDebInfo или Release):
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
// На машине, где писался эталон (GCC, Release): без ошибок около 165 нс на
// строку в обоих путях (разница в пределах шума), с ошибкой в каждой строке —
// около 3200 против 320 нс.

#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "taskq/errors.hpp"
#include "taskq/parser.hpp"
#include "workload.hpp"

namespace {

using Clock = std::chrono::steady_clock;

// InputLine нельзя ни копировать, ни перемещать, поэтому строки живут
// в unique_ptr: вектор при росте перемещает указатели, а не сами строки.
std::vector<std::unique_ptr<taskq::InputLine>> make_lines(const std::vector<std::string>& texts) {
    std::vector<std::unique_ptr<taskq::InputLine>> lines;
    lines.reserve(texts.size());
    for (const std::string& text : texts) {
        lines.push_back(std::make_unique<taskq::InputLine>(text));
    }
    return lines;
}

// Время на одну строку в наносекундах. Счётчик успехов нужен, чтобы
// оптимизатор не выбросил разбор как не имеющий наблюдаемого эффекта.
template <class Parse>
double nanoseconds_per_line(const std::vector<std::unique_ptr<taskq::InputLine>>& lines,
                            int rounds, Parse parse_one, std::size_t& successes) {
    const auto start = Clock::now();
    for (int round = 0; round < rounds; ++round) {
        for (const auto& line : lines) {
            successes += parse_one(*line) ? 1 : 0;
        }
    }
    const std::chrono::duration<double, std::nano> elapsed = Clock::now() - start;
    return elapsed.count() / static_cast<double>(lines.size() * static_cast<std::size_t>(rounds));
}

bool with_exceptions(const taskq::InputLine& line) {
    try {
        static_cast<void>(taskq::parse(line));
        return true;
    } catch (const taskq::ParseError&) {
        return false;
    }
}

bool with_result(const taskq::InputLine& line) {
    return taskq::try_parse(line).has_value();
}

void report(const char* title, const std::vector<std::string>& texts, int rounds) {
    const auto lines = make_lines(texts);
    std::size_t successes = 0;
    const double thrown = nanoseconds_per_line(lines, rounds, with_exceptions, successes);
    const double returned = nanoseconds_per_line(lines, rounds, with_result, successes);
    std::cout << title << '\n'
              << "  exceptions: " << thrown << " ns per line\n"
              << "  Result:     " << returned << " ns per line\n"
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
