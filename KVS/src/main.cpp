#include <iostream>
#include <string>
#include <utility>

#include "executor.hpp"
#include "parser.hpp"
#include "store.hpp"

namespace {
void print_help() {
    std::cout << "Commands:\n"
              << "  SET <key> <value>   write down\n"
              << "  GET <key>           read\n"
              << "  DEL <key>           delete\n"
              << "  KEYS                list the keys\n"
              << "  COUNT               count of records\n"
              << "  DEMO                difference in grips in the lambda\n"
              << "  HELP / QUIT\n";
}

void run_capture_demo(const Store& store) {
    int by_reference = 0;
    store.for_each([&by_reference](const std::string&, const Value& v) {
        by_reference += static_cast<int>(v.size());
    });

    int by_value = 0;
    store.for_each([by_value](const std::string&, const Value& v) mutable {
        by_value += static_cast<int>(v.size());
    });

    std::cout << "capture by reference: " << by_reference << '\n'
              << "capture by value:     " << by_value
              << "   (copy inside the lambda changed)\n";
}

}  // namespace

int main() {
    std::cout << "KVS, stage 2\n";
    print_help();

    std::string raw;
    while (std::getline(std::cin, raw)) {
        Store store;
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
        if (view == "KEYS") {
            store.for_each([](const std::string& key, const Value&) {
                std::cout << key << '\n';
            });
            continue;
        }

        if (const auto command = parse(line); command) {
            std::cout << execute(store, *command).text << '\n';
        } else {
            std::cout << "(not parsed; HELP will show a list of commands)\n";
        }
    }
    return 0;
}
