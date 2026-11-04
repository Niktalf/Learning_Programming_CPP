#include <charconv>
#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "kvs/handler.hpp"
#include "kvs/store.hpp"

namespace {

void print_help() {
    std::cout << "Commands:\n"
              << "  SET <key> <value>          store a value (quote values with spaces)\n"
              << "  MSET <k1> <v1> <k2> <v2>   store several, all or nothing\n"
              << "  GET <key>                  read a value\n"
              << "  DEL <key>                  delete a key\n"
              << "  EXISTS <key>               1 or 0\n"
              << "  TYPE <key>                 type of the value\n"
              << "  INCR <key> [step]          increment an integer\n"
              << "  KEYS                       list keys\n"
              << "  MEMORY                     used bytes and the limit\n"
              << "  DEMO                       capture by reference vs by value\n"
              << "  HELP / QUIT\n";
}

void print_usage() {
    std::cerr << "usage: kvs_cli [--maxmemory=BYTES]\n";
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

// Разбор аргумента --maxmemory=N. Неизвестный аргумент — отказ запуска, а не
// молчаливое игнорирование: опечатка в «--maxmemroy» иначе дала бы хранилище
// без предела, и заметил бы это только тот, у кого кончилась память.
[[nodiscard]] std::optional<std::size_t> parse_limit(std::string_view argument) {
    constexpr std::string_view prefix = "--maxmemory=";
    if (argument.substr(0, prefix.size()) != prefix) {
        return std::nullopt;
    }
    const std::string_view digits = argument.substr(prefix.size());
    std::size_t limit = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), limit);
    if (digits.empty() || error != std::errc{} || end != digits.data() + digits.size()) {
        return std::nullopt;
    }
    return limit;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc > 2) {
        print_usage();
        return 2;
    }
    const std::optional<std::size_t> limit =
        argc == 2 ? parse_limit(argv[1]) : std::optional<std::size_t>{};
    if (argc == 2 && !limit) {
        print_usage();
        return 2;
    }
    // Без аргумента хранилище не ограничено.
    kvs::Store store = limit ? kvs::Store(*limit) : kvs::Store();

    std::cout << "KVS, stage 3\n";
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
            run_capture_demo(store);
            continue;
        }
        if (view == "KEYS") {
            store.for_each([](const std::string& key, const kvs::Value&) {
                std::cout << key << '\n';
            });
            continue;
        }
        if (view == "MEMORY") {
            std::cout << "used " << store.used_bytes() << " bytes, limit ";
            if (const auto store_limit = store.memory_limit(); store_limit) {
                std::cout << *store_limit << " bytes\n";
            } else {
                std::cout << "none\n";
            }
            continue;
        }
        // Вся обработка ошибок — внутри handle. Здесь нет ни одного try:
        // консольный клиент просто печатает ответ, каким бы он ни был.
        std::cout << kvs::handle(store, std::move(raw)).text << '\n';
    }
    return 0;
}
