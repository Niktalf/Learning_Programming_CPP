#pragma once

#include <algorithm>
#include <array>
#include <cstdlib>
#include <optional>
#include <string_view>

namespace kvs {

// Этап 9: выбор движка отделён от самих движков. Это конфигурация — её
// читают точка сборки (src/compose.cpp) и консольный клиент, — и ей не нужно
// знать ни одной хеш-таблицы. До этапа 9 перечисление лежало в engine.hpp
// рядом с реализациями, и всякий, кто хотел лишь разобрать --engine,
// включал все таблицы.
// Какой движок.
enum class EngineKind {
    Std,       // std::unordered_map, стандартный хеш
    Hash,      // собственная таблица, цепочки, FNV-1a со случайной затравкой
    BadHash,   // та же таблица с суммой кодов символов — для контраста
    Open,      // звёздочка: открытая адресация, линейное пробирование, FNV-1a
};

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

inline constexpr std::array<EngineKind, 4> all_engine_kinds = {EngineKind::Std, EngineKind::Hash,
                                                               EngineKind::BadHash, EngineKind::Open};

[[nodiscard]] inline std::optional<EngineKind> parse_engine_kind(std::string_view name) noexcept {
    // Поиск по проекции: find сравнивает с name не сам элемент, а name_of
    // от него.
    const auto found = std::ranges::find(all_engine_kinds, name, name_of);
    return found == all_engine_kinds.end() ? std::nullopt : std::optional<EngineKind>(*found);
}

// Движок по умолчанию — из переменной окружения KVS_ENGINE, иначе std. Так
// весь набор тестов прогоняется на каждом движке без единой правки: ctest
// запускает его четыре раза с разным KVS_ENGINE (CMakeLists.txt). Неизвестное
// имя — std: тесты не должны падать из-за опечатки в окружении, а консольный
// клиент проверяет --engine сам и отказывает с ошибкой.
[[nodiscard]] inline EngineKind default_engine_kind() noexcept {
    static const EngineKind kind = [] {
        const char* const name = std::getenv("KVS_ENGINE");
        return name == nullptr ? EngineKind::Std : parse_engine_kind(name).value_or(EngineKind::Std);
    }();
    return kind;
}


}  // namespace kvs
