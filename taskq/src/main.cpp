#include <exception>
#include <cstdio>
#include <iostream>
#include <print>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "taskq/executor.hpp"
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
  LIST <queue> [prio>=N] [type=K] [limit=N]
                                   tasks waiting in a queue
  ACK <id>                         mark a leased task done
  FAIL <id>                        mark a leased task failed
  RELEASE <id>                     return a leased task to its queue
  STATUS <id>                      state of the task
  TYPE <id>                        kind of the payload
  BUMP <id> [n]                    add to a numeric payload
  WORKER <name>                    register a worker
  GONE <name>                      the worker disappeared
  REAP                             requeue tasks of vanished workers
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

}  // namespace

int main(int argc, char* argv[]) {
    constexpr std::string_view log_prefix = "--log=";
    std::optional<std::string> log_path;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument.substr(0, log_prefix.size()) != log_prefix ||
            argument.size() == log_prefix.size()) {
            std::println(stderr, "usage: taskq_cli [--log=PATH]");
            return 2;
        }
        log_path = std::string(argument.substr(log_prefix.size()));
    }

    // Запуск — отдельная граница, не командная: журнал не открылся, и
    // работать дальше нельзя. Сервис строится в optional через emplace, без
    // кучи, — только затем, чтобы конструктор оказался внутри try.
    std::optional<taskq::Service> service;
    try {
        if (log_path) {
            service.emplace(*log_path);
        } else {
            service.emplace();
        }
    } catch (const std::exception& e) {
        std::println(stderr, "cannot start: {}", e.what());
        return 1;
    }

    std::println("TASKQ, stage 5");
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
