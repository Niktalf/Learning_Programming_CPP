#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "taskq/executor.hpp"
#include "taskq/parser.hpp"

namespace {

void print_help() {
    std::cout << "Commands:\n"
              << "  SUBMIT <queue> [prio=N] <body>   enqueue a task, priority 0..9;\n"
              << "                                   quote queue names with spaces;\n"
              << "                                   body num:42 is a number, bin:cafe bytes\n"
              << "  POLL <queue>                     take the next task\n"
              << "  ACK <id>                         mark a taken task done\n"
              << "  FAIL <id>                        mark a taken task failed\n"
              << "  STATUS <id>                      where the task is\n"
              << "  TYPE <id>                        kind of the payload\n"
              << "  BUMP <id> [n]                    add to a numeric payload\n"
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

int main() {
    std::cout << "TASKQ, stage 2\n";
    print_help();

    taskq::Service service;
    std::string raw;   // объявлена до цикла: в неё читает std::getline
    while (std::getline(std::cin, raw)) {
        // Владелец строки живёт дольше результата разбора — в этом вся суть
        // архитектурного исправления этапа.
        const taskq::InputLine line{std::move(raw)};

        const std::string_view view = line.view();
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
            run_capture_demo(service.store());
            continue;
        }

        if (const auto command = taskq::parse(line); command) {
            std::cout << service.execute(*command).text << '\n';
        } else {
            std::cout << "ERR unknown command or wrong arguments; HELP lists commands\n";
        }
    }
    return 0;
}
