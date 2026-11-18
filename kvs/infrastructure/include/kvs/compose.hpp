#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>

#include "kvs/clock.hpp"
#include "kvs/engine_kind.hpp"
#include "kvs/kafka_publisher.hpp"
#include "kvs/server.hpp"

namespace kvs {

// Этап 9: точка сборки (composition root). Единственное место ядра, где
// создаются конкретные классы: движок по имени, файловый журнал по пути,
// часы. Всё остальное получает зависимости готовыми и знает их только по
// интерфейсам. Контейнер внедрения зависимостей на этом масштабе не нужен —
// хватает одной функции.
struct Config {
    std::optional<std::size_t> memory_limit;     // --maxmemory
    std::optional<std::filesystem::path> log;    // --log
    EngineKind engine = EngineKind::Std;          // --engine
    // Этап 10: публикация событий в Kafka. Пусто — события не публикуются
    // (--events включает, --brokers и --topic уточняют).
    std::optional<KafkaOptions> events;
};

// Бросает, если журнал не открылся или не воспроизвёлся, а также если
// события запрошены, а адаптер Kafka не собран.
[[nodiscard]] Server make_server(const Config& config, const IClock& clock = steady_clock());

}  // namespace kvs
