#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "taskq/executor.hpp"
#include "taskq/handler.hpp"

namespace {

void print_help() {
    std::cout << "Commands:\n"
              << "  CREATE <queue>                   create a queue\n"
              << "  SUBMIT <queue> [prio=N] <body>   enqueue a task, priority 0..9;\n"
              << "                                   quote queue names with spaces;\n"
              << "                                   body num:42 is a number, bin:cafe bytes\n"
              << "  SUBMITBATCH <queue> [prio=N] <body> ...\n"
              << "                                   enqueue several, all or nothing;\n"
              << "                                   quote bodies with spaces\n"
              << "  POLL <queue> [worker]            lease the next task, optionally to a worker\n"
              << "  ACK <id>                         mark a leased task done\n"
              << "  FAIL <id>                        mark a leased task failed\n"
              << "  RELEASE <id>                     return a leased task to its queue\n"
              << "  STATUS <id>                      state of the task\n"
              << "  TYPE <id>                        kind of the payload\n"
              << "  BUMP <id> [n]                    add to a numeric payload\n"
              << "  WORKER <name>                    register a worker\n"
              << "  GONE <name>                      the worker disappeared\n"
              << "  REAP                             requeue tasks of vanished workers\n"
              << "  DEMO                             capture by reference vs by value\n"
              << "  HELP / QUIT\n";
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

    std::cout << "captured by reference: " << by_reference << '\n'
              << "captured by value:     " << by_value
              << "   (the lambda changed its own copy)\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    constexpr std::string_view log_prefix = "--log=";
    std::optional<std::string> log_path;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument.substr(0, log_prefix.size()) != log_prefix ||
            argument.size() == log_prefix.size()) {
            std::cerr << "usage: taskq_cli [--log=PATH]\n";
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
        std::cerr << "cannot start: " << e.what() << '\n';
        return 1;
    }

    std::cout << "TASKQ, stage 4\n";
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
        std::cout << taskq::handle(*service, std::move(raw)).text << '\n';
    }
    return 0;
}
