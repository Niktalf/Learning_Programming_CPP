#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "errors.hpp"
#include "handler.hpp"
#include "log.hpp"
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
                  << "  RPUSH <key> <elements>          push to end of list\n"
                  << "  LLEN <key>                      length of list\n"
                  << "  KEYS                            list the keys\n"
                  << "  HELP / QUIT\n";
    }
}  // namespace

int main() {
    try {
        Store store;
        AppendOnlyLog log("kvs.log");
        std::cout << "KVS, stage 4\n";
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
            if (view == "KEYS") {
                store.for_each([](const std::string& key, const Value&) {
                    std::cout << key << '\n';
                });
                continue;
            }
            std::string record = raw;
            const auto [ok, text] = handle(store, std::move(raw));
            if (ok) {
                log.append(record);
            }
            std::cout << text << '\n';
        }
    } catch (const KvError& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
