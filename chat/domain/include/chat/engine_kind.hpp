#pragma once

#include <algorithm>
#include <array>
#include <cstdlib>
#include <optional>
#include <string_view>

namespace chat {

// Этап 9: выбор движка отделён от самих движков — это конфигурация, и ей не
// нужно знать ни одной хеш-таблицы. До этапа 9 перечисление лежало в
// engine.hpp рядом с реализациями.
enum class EngineKind { Std, Hash, BadHash, Open };

inline constexpr std::array<EngineKind, 4> all_engine_kinds = {EngineKind::Std, EngineKind::Hash,
                                                               EngineKind::BadHash, EngineKind::Open};

[[nodiscard]] constexpr std::string_view name_of(EngineKind kind) noexcept {
    switch (kind) {
        case EngineKind::Std:
            return "std";
        case EngineKind::Hash:
            return "hash";
        case EngineKind::BadHash:
            return "bad-hash";
        case EngineKind::Open:
            return "open";
    }
    return "std";
}

// Поиск по проекции: find сравнивает с name не сам элемент, а name_of от
// него.
[[nodiscard]] inline std::optional<EngineKind> parse_engine_kind(std::string_view name) noexcept {
    const auto found = std::ranges::find(all_engine_kinds, name, name_of);
    return found == all_engine_kinds.end() ? std::nullopt : std::optional<EngineKind>(*found);
}

// Движок по умолчанию — из переменной окружения CHAT_ENGINE, иначе std. Так
// весь набор тестов прогоняется на каждом движке без единой правки: ctest
// запускает его четыре раза с разным CHAT_ENGINE (CMakeLists.txt).
// Неизвестное имя — std: консольный клиент проверяет --engine сам и
// отвергает опечатку с сообщением.
[[nodiscard]] inline EngineKind default_engine_kind() noexcept {
    const char* const value = std::getenv("CHAT_ENGINE");   // NOLINT: окружение читается до потоков
    return value == nullptr ? EngineKind::Std : parse_engine_kind(value).value_or(EngineKind::Std);
}


}  // namespace chat
