#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "kvs/executor.hpp"
#include "kvs/parser.hpp"
#include "kvs/store.hpp"

namespace {

void print_help() {
    std::cout << "Commands:\n"
              << "  SET <key> <value>   store a value (quote values with spaces)\n"
              << "  GET <key>           read a value\n"
              << "  DEL <key>           delete a key\n"
              << "  EXISTS <key>        1 or 0\n"
              << "  TYPE <key>          type of the value\n"
              << "  INCR <key> [step]   increment an integer\n"
              << "  KEYS                list keys\n"
              << "  DEMO                capture by reference vs by value\n"
              << "  HELP / QUIT\n";
}

void run_capture_demo(const kvs::Store& store) {
    int by_reference = 0;
    store.for_each([&by_reference](const std::string&, const kvs::Value& value) {
        by_reference += static_cast<int>(value.size());
    });

    int by_value = 0;
    store.for_each([by_value](const std::string&, const kvs::Value& value) mutable {
        by_value += static_cast<int>(value.size());
    });

    std::cout << "captured by reference: " << by_reference << '\n'
              << "captured by value:     " << by_value
              << "   (the lambda changed its own copy)\n";
}

}  // namespace

int main() {
    std::cout << "KVS, stage 2\n";
    print_help();

    kvs::Store store;
    std::string raw;   // объявлена до цикла: в неё читает std::getline
    while (std::getline(std::cin, raw)) {
        // Владелец строки живёт дольше результата разбора — в этом вся суть
        // архитектурного исправления этапа. Команда смотрит внутрь line,
        // и line не перемещается и не разрушается раньше времени.
        const kvs::InputLine line{std::move(raw)};

        const std::string_view view = line.view();
        if (view.find_first_not_of(' ') == std::string_view::npos) {
            continue;   // пустая строка — не ошибка, просто нечего делать
        }
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
            store.for_each([](const std::string& key, const kvs::Value&) {
                std::cout << key << '\n';
            });
            continue;
        }

        if (const auto command = kvs::parse(line); command) {
            std::cout << kvs::execute(store, *command).text << '\n';
        } else {
            std::cout << "ERR unknown command or wrong arguments; HELP lists commands\n";
        }
    }
    return 0;
}
