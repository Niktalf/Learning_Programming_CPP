#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "errors.hpp"
#include "handler.hpp"
#include "log.hpp"
#include "task_store.hpp"

namespace {
    void print_help() {
        std::cout << "Commands:\n"
                  << "  CREATE <queue>                          create a queue\n"
                  << "  WORKER <name> / GONE <name>             worker appeared / disappeared\n"
                  << "  SUBMIT <queue> <priority> <body>        submit a task\n"
                  << "  BATCH <q1,q2,…> <priority> <body>       in all queues or none\n"
                  << "  POLL <queue>                            to rent a task\n"
                  << "  ACK <id> / FAIL <id> / RELEASE <id>     completed / failed / return\n"
                  << "  STATUS <id> / TYPE <id>                 body status / type\n"
                  << "  BUMP <id> [step]                        change the numeric body\n"
                  << "  QUEUES                                  list the queues\n"
                  << "  HELP / QUIT\n";
    }
}  // namespace

int main() {
    TaskStore store;
    Service service{store};

    try {
        AppendOnlyLog log("taskq.log");
        std::cout << "TASKQ, stage 4\n";
        print_help();

        std::string raw;
        while (std::getline(std::cin, raw)) {
            const std::string_view view = raw;
            if (view == "QUIT") {
                break;
            }
            if (view == "HELP") {
                print_help();
                continue;
            }
            std::string record = raw;
            const auto [ok, text] = handle(service, std::move(raw));
            if (ok) {
                log.append(record);
            }
            std::cout << text;
            if (text.empty() || text.back() != '\n') {
                std::cout << '\n';
            }
        }
    } catch (const TaskQError& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
