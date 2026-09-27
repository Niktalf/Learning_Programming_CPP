#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "executor.hpp"
#include "parser.hpp"
#include "task_store.hpp"

namespace {
    void print_help() {
        std::cout << "Commands:\n"
                  << "  SUBMIT <queue> <body>   submit a task\n"
                  << "  PEEK <queue>            look at the first one without taking it\n"
                  << "  TAKE <queue>            take the first one\n"
                  << "  QUEUES                  list the queues\n"
                  << "  DEMO                    difference in captures in lambda\n"
                  << "  HELP / QUIT\n";
    }
    void run_capture_demo(const TaskStore& store) {
        int by_reference = 0;
        store.for_each_queue([&by_reference](const std::string&, const Queue& tasks) {
            by_reference += static_cast<int>(tasks.size());
        });

        int by_value = 0;
        store.for_each_queue([by_value](const std::string&, const Queue& tasks) mutable {
            by_value += static_cast<int>(tasks.size());
        });

        std::cout << "capture by reference:   " << by_reference << '\n'
                  << "capture by value: " << by_value
                  << "   (the copy was modified inside the lambda)\n";
    }
}  // namespace

int main() {
    TaskStore store;
    Service service{store};
    std::cout << "TASKQ, stage 2\n";
    print_help();

    std::string raw;
    while (std::getline(std::cin, raw)) {
        const InputLine line{std::move(raw)};

        const std::string_view view = line.view();
        if (view == "QUIT") {
            break;
        }
        if (view == "HELP") {
            print_help();
            continue;
        }
        if (view == "DEMO") {
            run_capture_demo(store);
            continue;
        }

        if (const auto command = parse(line); command) {
            const auto [ok, text] = service.execute(*command);
            std::cout << text;
            if (text.empty() || text.back() != '\n') {
                std::cout << '\n';
            }
        } else {
            std::cout << "(not parsed; HELP will show a list of commands)\n";
        }
    }
    return 0;
}
