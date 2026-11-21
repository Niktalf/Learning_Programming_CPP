// kvs_tail — подписчик на события KVS (этап 10).
//
// Читает топик kvs.events и печатает по строке на событие: партиция,
// смещение, ключ сообщения и само событие. Запускается во втором окне рядом
// с kvs_cli --events; останавливается по Ctrl+C.
//
// Звёздочка: потребитель идемпотентен. Смещения фиксируются вручную, после
// того как пачка напечатана (auto_commit = false), — это at-least-once:
// упавший до фиксации потребитель после перезапуска получит пачку снова.
// Повторы узнаются по идентификатору события (SeenEvents) и пропускаются.
// Пропуски — по поколениям записи (GenerationTracker).
//
// С --state=FILE окно идентификаторов переживает перезапуск. Порядок шагов
// на каждую пачку: напечатать → дописать идентификаторы в файл → commit.
// Упал после файла, до commit — пачка придёт снова и будет узнана целиком.
// Упал между печатью и файлом — придёт снова и напечатается второй раз:
// печать и файл не атомарны вместе, и exactly-once без транзакций, которых
// у обёртки нет, недостижим (docs/events.md).

#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <fstream>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "kvs/event_wire.hpp"
#include "kvs/seen_events.hpp"
#include "lab/kafka/event_bus.hpp"

namespace {

std::atomic<bool> stop_requested{false};

extern "C" void on_signal(int) { stop_requested = true; }

struct TailOptions {
    std::string brokers = "localhost:9092";
    std::string topic = "kvs.events";
    std::string group = "kvs-tail";
    std::size_t remember = 100000;   // сколько идентификаторов помнить
    bool from_beginning = true;
    std::optional<std::string> state;   // файл окна идентификаторов
};

void print_usage() {
    std::println(stderr,
                 "usage: kvs_tail [--brokers=HOST:PORT] [--topic=NAME] [--group=ID] [--remember=N] [--latest]\n"
                         "                [--state=FILE]");
}

[[nodiscard]] std::optional<std::size_t> to_size(std::string_view digits) noexcept {
    std::size_t value = 0;
    const char* const last = digits.data() + digits.size();
    const auto [end, error] = std::from_chars(digits.data(), last, value);
    if (digits.empty() || error != std::errc{} || end != last) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<TailOptions> parse_arguments(int argc, char* argv[]) {
    TailOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        const auto value_of = [argument](std::string_view prefix) -> std::optional<std::string_view> {
            if (!argument.starts_with(prefix) || argument.size() == prefix.size()) {
                return std::nullopt;
            }
            return argument.substr(prefix.size());
        };
        if (const auto brokers = value_of("--brokers=")) {
            options.brokers = *brokers;
        } else if (const auto topic = value_of("--topic=")) {
            options.topic = *topic;
        } else if (const auto group = value_of("--group=")) {
            options.group = *group;
        } else if (const auto remember = value_of("--remember=")) {
            const std::optional<std::size_t> count = to_size(*remember);
            if (!count || *count == 0) {
                return std::nullopt;
            }
            options.remember = *count;
        } else if (const auto state = value_of("--state=")) {
            options.state = std::string(*state);
        } else if (argument == "--latest") {
            options.from_beginning = false;
        } else {
            return std::nullopt;
        }
    }
    return options;
}

// Окно идентификаторов с прошлого запуска: строка — идентификатор. Файла
// нет — первый запуск. Окно само ограничит число запомненных.
void load_state(const std::string& path, kvs::SeenEvents& seen) {
    std::ifstream in(path);
    std::size_t lines = 0;
    for (std::string id; std::getline(in, id); ++lines) {
        static_cast<void>(seen.first_time(id));
    }
    // Файл только дописывается; когда он вдвое длиннее окна, он
    // переписывается тем, что окно действительно помнит.
    if (lines > 2 * seen.size()) {
        std::ofstream out(path, std::ios::trunc);
        for (const std::string& id : seen.ids()) {
            out << id << '\n';
        }
    }
}

// Дописать идентификаторы пачки. Бросает, если запись не удалась: фиксировать
// смещение, не сохранив окно, нельзя — повторы после падения пройдут.
void save_state(const std::string& path, const std::vector<std::string>& ids) {
    std::ofstream out(path, std::ios::app);
    for (const std::string& id : ids) {
        out << id << '\n';
    }
    out.flush();
    if (!out) {
        throw std::runtime_error("cannot write " + path);
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::optional<TailOptions> options = parse_arguments(argc, argv);
    if (!options) {
        print_usage();
        return 2;
    }
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    try {
        lab::kafka::Consumer consumer({
            .topic = options->topic,
            .brokers = options->brokers,
            .group_id = options->group,
            .from_beginning = options->from_beginning,
            .auto_commit = false,
            .join_timeout = std::chrono::milliseconds(15000),
        });
        std::println(stderr, "kvs_tail: reading {} from {}, group {}", options->topic, options->brokers,
                     options->group);

        kvs::SeenEvents seen(options->remember);
        if (options->state) {
            load_state(*options->state, seen);
        }
        kvs::GenerationTracker generations;
        while (!stop_requested) {
            const std::vector<lab::kafka::Record> batch = consumer.poll(std::chrono::milliseconds(500));
            if (batch.empty()) {
                continue;   // за полсекунды ничего не пришло — штатно
            }
            std::vector<std::string> fresh;   // новые идентификаторы этой пачки
            for (const lab::kafka::Record& record : batch) {
                const std::optional<kvs::EventEnvelope> envelope = kvs::decode_event(record.value);
                if (!envelope) {
                    std::println(stderr, "[{}:{}] not a KVS event: {}", record.partition, record.offset, record.value);
                    continue;
                }
                if (!seen.first_time(envelope->id)) {
                    std::println(stderr, "[{}:{}] duplicate {} skipped", record.partition, record.offset,
                                 envelope->id);
                    continue;
                }
                fresh.push_back(envelope->id);
                if (const auto gap = generations.observe(envelope->event)) {
                    std::println(stderr, "[{}:{}] gap on '{}': expected generation {}, got {}", record.partition,
                                 record.offset, record.key, gap->expected, gap->got);
                }
                std::println("[{}:{}] {}", record.partition, record.offset, kvs::describe_event(envelope->event));
            }
            std::fflush(stdout);
            if (options->state) {
                save_state(*options->state, fresh);
            }
            // Пачка обработана — теперь её можно фиксировать. До этой строки
            // падение означает повтор пачки, а не потерю.
            consumer.commit();
        }
    } catch (const std::exception& e) {
        std::println(stderr, "kvs_tail: {}", e.what());
        return 1;
    }
    return 0;
}
