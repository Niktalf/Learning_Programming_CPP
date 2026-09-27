#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "chat_store.hpp"
#include "executor.hpp"
#include "parser.hpp"

namespace {
    void print_help() {
        std::cout << "Commands:\n"
                  << "  /join #room             create a room\n"
                  << "  /leave #room            delete a room\n"
                  << "  /msg #room <text>       send a message\n"
                  << "  /history #room          show history\n"
                  << "  /rooms                  list rooms\n"
                  << "  /type #room N           message type\n"
                  << "  /react #room N [step]   change reaction counter\n"
                  << "  DEMO / HELP / QUIT\n";
    }


    void run_capture_demo(const ChatStore& store) {
        int by_reference = 0;
        store.for_each_room([&by_reference](const std::string&, const History& history) {
            by_reference += static_cast<int>(history.size());
        });

        int by_value = 0;
        store.for_each_room([by_value](const std::string&, const History& history) mutable {
            by_value += static_cast<int>(history.size());
        });

        std::cout << "capture by reference:   " << by_reference << '\n'
                  << "capture by value: " << by_value
                  << "   (the copy was modified inside the lambda)\n";
    }

}  // namespace

int main() {
    ChatStore store;
    std::cout << "CHAT, stage 2\n";
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
            const auto [ok, text] = execute(store, *command);
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
