#include <charconv>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>

#include "taskq/service.hpp"
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
  FAIL <id>                        mark a leased task failed
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

struct Arguments {
    std::optional<std::filesystem::path> log;
    taskq::EngineKind engine = taskq::default_engine_kind();
    std::chrono::seconds lease_timeout = taskq::Service::default_lease_timeout;
};

// Разбор аргументов: --log=PATH, --engine=NAME, --lease=SECONDS в любом
// порядке. Неизвестный ключ, движок или срок — отказ запуска, а не молчаливое
// игнорирование.
[[nodiscard]] std::optional<Arguments> parse_arguments(int argc, char* argv[]) {
    constexpr std::string_view log_prefix = "--log=";
    constexpr std::string_view engine_prefix = "--engine=";
    constexpr std::string_view lease_prefix = "--lease=";
    Arguments arguments;
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
            const std::string_view digits = argument.substr(lease_prefix.size());
            long long seconds = 0;
            const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), seconds);
            if (error != std::errc{} || end != digits.data() + digits.size() || seconds <= 0) {
                return std::nullopt;
            }
            arguments.lease_timeout = std::chrono::seconds(seconds);
        } else {
            return std::nullopt;
        }
    }
    return arguments;
}

}  // namespace

int main(int argc, char* argv[]) {
    const auto arguments = parse_arguments(argc, argv);
    if (!arguments) {
        std::println(stderr, "usage: taskq_cli [--log=PATH] [--engine=std|hash|bad-hash|open] [--lease=SECONDS]");
        return 2;
    }

    // Запуск — отдельная граница, не командная: журнал не открылся, и
    // работать дальше нельзя. Сервис строится в optional через emplace, без
    // кучи, — только затем, чтобы конструктор оказался внутри try.
    std::optional<taskq::Service> service;
    try {
        service.emplace(arguments->engine, arguments->log);
    } catch (const std::exception& e) {
        std::println(stderr, "cannot start: {}", e.what());
        return 1;
    }

    service->set_lease_timeout(arguments->lease_timeout);
    std::println("TASKQ, stage 8, engine {}, lease {}s", service->store().engine_name(),
                 arguments->lease_timeout.count());
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
    return 0;
}
