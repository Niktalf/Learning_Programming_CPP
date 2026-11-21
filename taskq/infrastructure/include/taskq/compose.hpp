#pragma once

#include <chrono>
#include <filesystem>
#include <optional>

#include "taskq/clock.hpp"
#include "taskq/engine_kind.hpp"
#include "taskq/kafka_publisher.hpp"
#include "taskq/retry_policy.hpp"
#include "taskq/service.hpp"

namespace taskq {

// Этап 9: точка сборки (composition root). Единственное место ядра, где
// создаются конкретные классы: индекс по номеру по имени движка, файловый
// журнал по пути, часы. Всё остальное получает зависимости готовыми и знает
// их только по интерфейсам.
struct Config {
    std::optional<std::filesystem::path> log;                         // --log
    EngineKind engine = EngineKind::Std;                               // --engine
    std::chrono::seconds lease_timeout = Service::default_lease_timeout;   // --lease
    RetryPolicy retry;                                                  // --retries, --backoff
    // Этап 10: публикация событий в Kafka. Пусто — события не публикуются
    // (--events включает, --brokers и --topic уточняют).
    std::optional<KafkaOptions> events;
};

// Бросает, если журнал не открылся.
[[nodiscard]] Service make_service(const Config& config, const IClock& clock = system_clocks());

}  // namespace taskq
