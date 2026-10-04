#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "handler.hpp"
#include "task_store.hpp"

namespace {
    void print_help() {
        std::cout << "Commands:\n"
                  << "  CREATE <queue>                          create a queue\n"
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
    std::cout << "TASKQ, stage 3\n";
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
        const auto [ok, text] = handle(service, std::move(raw));
        std::cout << text;
        if (text.empty() || text.back() != '\n') {
            std::cout << '\n';
        }
    }
    return 0;
}
