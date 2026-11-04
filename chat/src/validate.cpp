#include "chat/validate.hpp"

#include <string>

namespace chat {
namespace {

[[nodiscard]] bool is_name_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '-';
}

[[nodiscard]] bool in_range(unsigned char c, unsigned char low, unsigned char high) noexcept {
    return c >= low && c <= high;
}

[[nodiscard]] std::optional<std::string> name_problem(std::string_view name,
                                                      std::string_view what) {
    if (name.empty()) {
        return std::string(what) + " is empty";
    }
    if (name.size() > max_name_length) {
        return std::string(what) + " is longer than " + std::to_string(max_name_length) +
               " characters";
    }
    for (const char c : name) {
        if (!is_name_char(c)) {
            return std::string(what) + " may contain only letters, digits, _ and -";
        }
    }
    return std::nullopt;
}

}  // namespace

bool is_valid_utf8(std::string_view text) noexcept {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        if (lead < 0x80) {
            ++i;
            continue;
        }
        // Длина последовательности и допустимый диапазон второго байта.
        // Суженные диапазоны отсекают избыточные формы (E0, F0), суррогаты
        // (ED) и кодовые точки больше U+10FFFF (F4).
        std::size_t length = 0;
        unsigned char second_low = 0x80;
        unsigned char second_high = 0xBF;
        if (in_range(lead, 0xC2, 0xDF)) {
            length = 2;
        } else if (lead == 0xE0) {
            length = 3;
            second_low = 0xA0;
        } else if (in_range(lead, 0xE1, 0xEC) || in_range(lead, 0xEE, 0xEF)) {
            length = 3;
        } else if (lead == 0xED) {
            length = 3;
            second_high = 0x9F;
        } else if (lead == 0xF0) {
            length = 4;
            second_low = 0x90;
        } else if (in_range(lead, 0xF1, 0xF3)) {
            length = 4;
        } else if (lead == 0xF4) {
            length = 4;
            second_high = 0x8F;
        } else {
            return false;   // байт продолжения без начала, C0, C1 или F5 и выше
        }
        if (text.size() - i < length) {
            return false;   // последовательность обрезана
        }
        if (!in_range(static_cast<unsigned char>(text[i + 1]), second_low, second_high)) {
            return false;
        }
        for (std::size_t k = 2; k < length; ++k) {
            if (!in_range(static_cast<unsigned char>(text[i + k]), 0x80, 0xBF)) {
                return false;
            }
        }
        i += length;
    }
    return true;
}

std::optional<std::string> room_name_problem(std::string_view room) {
    if (room.empty() || room.front() != '#') {
        return "room name must start with #: " + std::string(room);
    }
    return name_problem(room.substr(1), "room name");
}

std::optional<std::string> nick_problem(std::string_view nick) {
    return name_problem(nick, "nickname");
}

std::optional<std::string> text_problem(std::string_view text) {
    if (text.find_first_not_of(' ') == std::string_view::npos) {
        return std::string("text is empty");
    }
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7F) {
            return std::string("text contains control characters");
        }
    }
    return std::nullopt;
}

}  // namespace chat
