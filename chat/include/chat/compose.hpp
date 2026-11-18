#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>

#include "chat/clock.hpp"
#include "chat/engine_kind.hpp"
#include "chat/server.hpp"

namespace chat {

// Этап 9: точка сборки (composition root). Единственное место ядра, где
// создаются конкретные классы: данные чата по имени движка, файловый журнал
// по пути, часы. Всё остальное получает зависимости готовыми и знает их
// только по интерфейсам.
struct Config {
    std::optional<std::filesystem::path> log;                     // --log
    EngineKind engine = EngineKind::Std;                           // --engine
    std::size_t history_capacity = ChatStore::default_history_capacity;
};

// Бросает, если журнал не открылся.
[[nodiscard]] Server make_server(const Config& config, const IClock& clock = system_clocks());

}  // namespace chat
