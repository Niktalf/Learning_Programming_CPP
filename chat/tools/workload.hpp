#pragma once

#include <string>
#include <vector>

namespace chat::tools {

// Тысяча команд, похожих на настоящие: треть /msg с текстом длиннее 15 символов,
// треть /join, треть /history с лимитом. Текст длиннее буфера коротких строк
// std::string — иначе копирующий лексер не платил бы за него аллокацией.
inline std::vector<std::string> sample_commands(int count = 1000) {
    std::vector<std::string> lines;
    lines.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const std::string room = "#room" + std::to_string(i % 50);
        switch (i % 3) {
            case 0:
                lines.push_back("/msg " + room + " message number " + std::to_string(i) +
                                " for everyone");
                break;
            case 1:
                lines.push_back("/join " + room);
                break;
            default:
                lines.push_back("/history " + room + " 5");
                break;
        }
    }
    return lines;
}

// Сколько токенов до хвоста у строки: /msg разбирается с хвостом на третьем.
inline std::size_t limit_for(const std::string& line) {
    return line.rfind("/msg", 0) == 0 ? 3 : static_cast<std::size_t>(-1);
}

}  // namespace chat::tools
