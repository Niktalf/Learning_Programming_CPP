#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "owning_buffer.hpp"
#include "store.hpp"
#include "value.hpp"

namespace {
std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream stream(line);
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

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
    Store store;

    const OwningBuffer banner(std::string("KVS, stage 1"));
    std::cout << banner.str() << '\n';
    print_help();

    std::string line;
    while (std::getline(std::cin, line)) {
        const std::vector<std::string> tokens = split(line);
        if (tokens.empty()) {
            continue;
        }
        const std::string& command = tokens.front();

        if (command == "QUIT") {
            break;
        } else if (command == "HELP") {
            print_help();
        } else if (command == "SET" && tokens.size() >= 3) {
            store.set(tokens[1], Value(tokens[2]));
            std::cout << "OK\n";
        } else if (command == "GET" && tokens.size() == 2) {
            if (const Value* found = store.get(tokens[1])) {
                std::cout << found->str() << '\n';
            } else {
                std::cout << "(no such key)\n";
            }
        } else if (command == "DEL" && tokens.size() == 2) {
            std::cout << (store.del(tokens[1]) ? "OK" : "(no such key)") << '\n';
        } else if (command == "KEYS") {
            store.for_each([](const std::string& key, const Value&) {
                std::cout << key << '\n';
            });
        } else if (command == "COUNT") {
            std::cout << store.size() << '\n';
        } else if (command == "DEMO") {
            run_capture_demo(store);
        } else {
            std::cout << "(not sorted out; HELP will show the list of commands.)\n";
        }
    }
    return 0;
}
