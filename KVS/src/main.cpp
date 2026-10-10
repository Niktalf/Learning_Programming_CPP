#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "handler.hpp"
#include "store.hpp"

namespace {
    void print_help() {
        std::cout << "Commands:\n"
                  << "  SET <key> <value>               write down\n"
                  << "  MSET <k1> <v1> <k2> <v2>...     write down a few, all or nothing\n"
                  << "  GET <key>                       read\n"
                  << "  DEL <key>                       delete\n"
                  << "  EXISTS <key>                    1 or 0\n"
                  << "  TYPE <key>                      Value type\n"
                  << "  INCR <key> [step]               increment integer\n"
                  << "  KEYS                            list the keys\n"
                  << "  HELP / QUIT\n";
    }
}  // namespace

int main() {
    std::cout << "KVS, stage 3\n";
    print_help();

    std::string raw;
    while (std::getline(std::cin, raw)) {
        Store store;
        const std::string_view view = raw;
        if (view == "QUIT") {
            break;
        }
        if (view == "HELP") {
            print_help();
            continue;
        }
        if (view == "KEYS") {
            store.for_each([](const std::string& key, const Value&) {
                std::cout << key << '\n';
            });
            continue;
        }
        std::cout << handle(store, std::move(raw)).text << '\n';
    }
    return 0;
}
