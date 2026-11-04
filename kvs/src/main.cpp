#include <charconv>
#include <cstddef>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "kvs/server.hpp"

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
              << "  RPUSH <key> <item>         append to a list\n"
              << "  LLEN <key>                 length of a list\n"
              << "  KEYS                       list keys\n"
              << "  MEMORY                     used bytes and the limit\n"
              << "  DEMO                       capture by reference vs by value\n"
              << "  HELP / QUIT\n";
}

void print_usage() {
    std::cerr << "usage: kvs_cli [--maxmemory=BYTES] [--log=PATH]\n";
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

[[nodiscard]] std::optional<std::size_t> to_size(std::string_view digits) noexcept {
    std::size_t value = 0;
    const char* const last = digits.data() + digits.size();
    const auto [end, error] = std::from_chars(digits.data(), last, value);
    if (digits.empty() || error != std::errc{} || end != last) {
        return std::nullopt;
    }
    return value;
}

// Разбор аргументов. Неизвестный аргумент — отказ запуска, а не молчаливое
// игнорирование: опечатка в «--maxmemroy» иначе дала бы хранилище без
// предела, и заметил бы это только тот, у кого кончилась память.
[[nodiscard]] std::optional<kvs::Server::Options> parse_arguments(int argc, char* argv[]) {
    constexpr std::string_view memory_prefix = "--maxmemory=";
    constexpr std::string_view log_prefix = "--log=";
    kvs::Server::Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument.substr(0, memory_prefix.size()) == memory_prefix) {
            options.memory_limit = to_size(argument.substr(memory_prefix.size()));
            if (!options.memory_limit) {
                return std::nullopt;
            }
        } else if (argument.substr(0, log_prefix.size()) == log_prefix &&
                   argument.size() > log_prefix.size()) {
            options.log = std::string(argument.substr(log_prefix.size()));
        } else {
            return std::nullopt;
        }
    }
    return options;
}

}  // namespace

int main(int argc, char* argv[]) {
    const auto options = parse_arguments(argc, argv);
    if (!options) {
        print_usage();
        return 2;
    }

    // Запуск — отдельная граница, не командная: журнал не открылся или не
    // воспроизвёлся, и работать дальше нельзя. Сервер строится в optional
    // через emplace, без кучи, — только затем, чтобы конструктор оказался
    // внутри try, а цикл команд снаружи.
    std::optional<kvs::Server> server;
    try {
        server.emplace(*options);
    } catch (const std::exception& e) {
        std::cerr << "cannot start: " << e.what() << '\n';
        return 1;
    }

    std::cout << "KVS, stage 4\n";
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
            run_capture_demo(server->store());
            continue;
        }
        if (view == "KEYS") {
            server->store().for_each([](const std::string& key, const kvs::Value&) {
                std::cout << key << '\n';
            });
            continue;
        }
        if (view == "MEMORY") {
            std::cout << "used " << server->store().used_bytes() << " bytes, limit ";
            if (const auto limit = server->store().memory_limit(); limit) {
                std::cout << *limit << " bytes\n";
            } else {
                std::cout << "none\n";
            }
            continue;
        }
        // Вся обработка ошибок команд — внутри handle. Здесь нет ни одного
        // try: консольный клиент просто печатает ответ, каким бы он ни был.
        std::cout << server->handle(std::move(raw)).text << '\n';
    }
    return 0;
}
