#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>

#include "taskq/compose.hpp"
#include "taskq/handler.hpp"

namespace {

// Этап 5: стандарт C++23, вывод через std::print. Текст справки — одна
// сырая строка: так её видно целиком, как она будет напечатана.
void print_help() {
    std::print(R"(Commands:
  CREATE <queue>                   create a queue
  SUBMIT <queue> [prio=N] <body>   enqueue a task, priority 0..9;
                                   quote queue names with spaces;
                                   body num:42 is a number, bin:cafe bytes
  SUBMITBATCH <queue> [prio=N] <body> ...
                                   enqueue several, all or nothing;
                                   quote bodies with spaces
  POLL <queue> [worker]            lease the next task: highest priority first,
                                   then the oldest; optionally to a worker
  LIST <queue> [prio>=N] [type=K] [limit=N] [after=P.S]
                                   ready tasks of a queue, a page at a time
  ACK <id>                         mark a leased task done
  FAIL <id>                        mark a leased task failed, or retry it (--retries)
  RELEASE <id>                     return a leased task to its queue
  STATUS <id>                      state of the task
  TYPE <id>                        kind of the payload
  BUMP <id> [n]                    add to a numeric payload
  WORKER <name>                    register a worker
  GONE <name>                      the worker disappeared
  REAP                             requeue tasks of vanished workers and
                                   expired leases, release due delayed tasks
  DELAY <id> <delay>               hold a pending task back: 90, 90s, 15m, 2h
  PURGE <queue>                    cancel all pending tasks of a queue
  PEEK <queue>                     the task POLL would lease, without leasing
  INFO <id>                        when a waiting or leased task was submitted
  DEMO                             capture by reference vs by value
  HELP / QUIT
)");
}

// Демонстрация этапа 1: один и тот же обход с двумя лямбдами.
void run_capture_demo(const taskq::TaskStore& store) {
    int by_reference = 0;
    store.for_each_queue([&by_reference](const std::string&, const taskq::Queue& tasks) {
        by_reference += static_cast<int>(tasks.size());
    });

    int by_value = 0;
    store.for_each_queue([by_value](const std::string&, const taskq::Queue& tasks) mutable {
        by_value += static_cast<int>(tasks.size());
    });

    std::println("captured by reference: {}", by_reference);
    std::println("captured by value:     {}   (the lambda changed its own copy)", by_value);
}

// Положительное целое из хвоста аргумента или пусто.
[[nodiscard]] std::optional<long long> positive(std::string_view digits) noexcept {
    long long value = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (error != std::errc{} || end != digits.data() + digits.size() || value <= 0) {
        return std::nullopt;
    }
    return value;
}

// Параметры событий: включить, если ещё не включены, и отдать для правки.
taskq::KafkaOptions& events_of(taskq::Config& config) {
    if (!config.events) {
        config.events.emplace();
    }
    return *config.events;
}

// Разбор аргументов в Config точки сборки: --log=PATH, --engine=NAME,
// --lease=SECONDS, --retries=N, --backoff=SECONDS в любом порядке.
// Неизвестный ключ или значение — отказ запуска, а не молчаливое
// игнорирование. Этап 9: движок по умолчанию — из TASKQ_ENGINE, как и у
// тестов; окружение читает консольный клиент, а не ядро.
[[nodiscard]] std::optional<taskq::Config> parse_arguments(int argc, char* argv[]) {
    constexpr std::string_view log_prefix = "--log=";
    constexpr std::string_view engine_prefix = "--engine=";
    constexpr std::string_view lease_prefix = "--lease=";
    constexpr std::string_view retries_prefix = "--retries=";
    constexpr std::string_view backoff_prefix = "--backoff=";
    constexpr std::string_view brokers_prefix = "--brokers=";
    constexpr std::string_view topic_prefix = "--topic=";
    taskq::Config arguments;
    arguments.engine = taskq::default_engine_kind();
    long long retries = 1;
    long long backoff = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument.starts_with(log_prefix) && argument.size() > log_prefix.size()) {
            arguments.log = std::filesystem::path(argument.substr(log_prefix.size()));
        } else if (argument.starts_with(engine_prefix)) {
            const auto engine = taskq::parse_engine_kind(argument.substr(engine_prefix.size()));
            if (!engine) {
                return std::nullopt;
            }
            arguments.engine = *engine;
        } else if (argument.starts_with(lease_prefix)) {
            const auto seconds = positive(argument.substr(lease_prefix.size()));
            if (!seconds) {
                return std::nullopt;
            }
            arguments.lease_timeout = std::chrono::seconds(*seconds);
        } else if (argument.starts_with(retries_prefix)) {
            const auto count = positive(argument.substr(retries_prefix.size()));
            if (!count || *count > 100) {
                return std::nullopt;
            }
            retries = *count;
        } else if (argument.starts_with(backoff_prefix)) {
            const auto seconds = positive(argument.substr(backoff_prefix.size()));
            if (!seconds) {
                return std::nullopt;
            }
            backoff = *seconds;
        } else if (argument == "--events") {
            // Этап 10: публикация событий. --brokers и --topic включают её
            // сами: брокер указывают затем, чтобы в него писать.
            events_of(arguments);
        } else if (argument.starts_with(brokers_prefix) && argument.size() > brokers_prefix.size()) {
            events_of(arguments).brokers = std::string(argument.substr(brokers_prefix.size()));
        } else if (argument.starts_with(topic_prefix) && argument.size() > topic_prefix.size()) {
            events_of(arguments).topic = std::string(argument.substr(topic_prefix.size()));
        } else {
            return std::nullopt;
        }
    }
    arguments.retry = taskq::RetryPolicy(static_cast<std::uint32_t>(retries), std::chrono::seconds(backoff));
    return arguments;
}

}  // namespace

int main(int argc, char* argv[]) {
    const auto arguments = parse_arguments(argc, argv);
    if (!arguments) {
        std::println(stderr, "usage: taskq_cli [--log=PATH] [--engine=std|hash|bad-hash|open] [--lease=SECONDS] "
                             "[--retries=N] [--backoff=SECONDS]\n"
                             "                 [--events] [--brokers=HOST:PORT] [--topic=NAME]");
        return 2;
    }

    // Запуск — отдельная граница, не командная: журнал не открылся, и
    // работать дальше нельзя. Сервис строится в optional через emplace, без
    // кучи, — только затем, чтобы конструктор оказался внутри try.
    std::optional<taskq::Service> service;
    try {
        service.emplace(taskq::make_service(*arguments));
    } catch (const std::exception& e) {
        std::println(stderr, "cannot start: {}", e.what());
        return 1;
    }

    std::println("TASKQ, stage 10, engine {}, lease {}s, attempts {}", service->store().engine_name(),
                 arguments->lease_timeout.count(), arguments->retry.attempts());
    if (arguments->events) {
        std::println("events: {} on {}", arguments->events->topic, arguments->events->brokers);
    }
    print_help();

    std::string raw;   // объявлена до цикла: в неё читает std::getline
    while (std::getline(std::cin, raw)) {
        const std::string_view view = raw;
        if (view.find_first_not_of(' ') == std::string_view::npos) {
            continue;
        }
        if (view == "QUIT") {
            break;
        }
        if (view == "HELP") {
            print_help();
            continue;
        }
        if (view == "DEMO") {
            run_capture_demo(service->store());
            continue;
        }
        // Вся обработка ошибок команд — внутри handle. Здесь нет ни одного
        // try: консольный клиент просто печатает ответ, каким бы он ни был.
        std::println("{}", taskq::handle(*service, std::move(raw)).text);
    }
    if (service->lost_events() > 0) {
        std::println(stderr, "events lost: {}", service->lost_events());
    }
    return 0;
}
