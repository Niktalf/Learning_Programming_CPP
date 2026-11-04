#pragma once

#include <string>
#include <vector>

namespace kvs::tools {

// Тысяча команд, похожих на настоящие: треть SET со значением в кавычках,
// треть GET, треть INCR с шагом. Значения у SET длиннее 15 символов — иначе
// они уместились бы во внутренний буфер std::string (оптимизация коротких
// строк) и копирующий лексер не платил бы за них аллокацией.
inline std::vector<std::string> sample_commands(int count = 1000) {
    std::vector<std::string> lines;
    lines.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const std::string key = "user:" + std::to_string(i);
        switch (i % 3) {
            case 0:
                lines.push_back("SET " + key + " \"profile value number " + std::to_string(i) + "\"");
                break;
            case 1:
                lines.push_back("GET " + key);
                break;
            default:
                lines.push_back("INCR " + key + " 5");
                break;
        }
    }
    return lines;
}

}  // namespace kvs::tools
