#pragma once

#include <string>
#include <vector>

namespace taskq::tools {

// Тысяча команд, похожих на настоящие: треть SUBMIT с телом длиннее 15 символов,
// треть POLL, треть ACK. Тело длиннее буфера коротких строк std::string —
// иначе копирующий лексер не платил бы за него аллокацией.
inline std::vector<std::string> sample_commands(int count = 1000) {
    std::vector<std::string> lines;
    lines.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const std::string queue = "queue" + std::to_string(i % 20);
        switch (i % 3) {
            case 0:
                lines.push_back("SUBMIT " + queue + " make -j8 target_" + std::to_string(i));
                break;
            case 1:
                lines.push_back("POLL " + queue);
                break;
            default:
                lines.push_back("ACK " + std::to_string(i));
                break;
        }
    }
    return lines;
}

// Сколько токенов до хвоста у строки: у SUBMIT без приоритета хвост третий.
inline std::size_t limit_for(const std::string& line) {
    return line.rfind("SUBMIT", 0) == 0 ? 3 : static_cast<std::size_t>(-1);
}

}  // namespace taskq::tools
