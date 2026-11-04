#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "kvs/owning_buffer.hpp"
#include "kvs/store.hpp"
#include "kvs/value.hpp"

namespace {

// Разбор строки на этом этапе самый прямолинейный: разбиение по пробелам
// с копированием в std::string. На этапе 2 он будет переписан на
// std::string_view, и разницу в количестве аллокаций мы измерим.
std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream stream(line);
    std::string token;   // объявлен до цикла: в него читает operator>>
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

void print_help() {
    std::cout << "Commands:\n"
              << "  SET <key> <value>   store a value\n"
              << "  GET <key>           read a value\n"
              << "  DEL <key>           delete a key\n"
              << "  KEYS                list keys\n"
              << "  COUNT               number of entries\n"
              << "  DEMO                capture by reference vs by value\n"
              << "  HELP / QUIT\n";
}

// Демонстрация из постановки: один и тот же обход с двумя лямбдами.
// Разница возникает потому, что for_each принимает функтор по значению.
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
    // Единственное место, где используется учебный класс: показываем, что он
    // работает, и на этом расстаёмся с ним.
    const kvs::OwningBuffer banner(std::string("KVS, stage 1"));
    std::cout << banner.str() << '\n';
    print_help();

    kvs::Store store;
    std::string line;   // объявлена до цикла: в неё читает std::getline
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
            store.set(tokens[1], kvs::Value(tokens[2]));
            std::cout << "OK\n";
        } else if (command == "GET" && tokens.size() == 2) {
            if (const kvs::Value* const found = store.get(tokens[1])) {
                std::cout << found->str() << '\n';
            } else {
                std::cout << "(nil)\n";
            }
        } else if (command == "DEL" && tokens.size() == 2) {
            std::cout << (store.del(tokens[1]) ? "OK" : "(nil)") << '\n';
        } else if (command == "KEYS") {
            store.for_each([](const std::string& key, const kvs::Value&) {
                std::cout << key << '\n';
            });
        } else if (command == "COUNT") {
            std::cout << store.size() << '\n';
        } else if (command == "DEMO") {
            run_capture_demo(store);
        } else {
            std::cout << "ERR unknown command or wrong arguments; HELP lists commands\n";
        }
    }
    return 0;
}
