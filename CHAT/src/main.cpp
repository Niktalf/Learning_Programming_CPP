#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "chat_store.hpp"
#include "handler.hpp"

namespace {
    void print_help() {
        std::cout << "Commands:\n"
                  << "  /join #room                 create a room\n"
                  << "  /leave #room                delete a room\n"
                  << "  /msg #k1 [#k2 …] text       text to send to one or more rooms\n"
                  << "  /history #room [N]          last N messages\n"
                  << "  /rooms                      list rooms\n"
                  << "  /type #room N               message type\n"
                  << "  /react #room N [step]       change reaction counter\n"
                  << "  /nick name                  take a nickname\n"
                  << "  HELP / QUIT\n";
    }
}  // namespace

int main() {
    ChatStore store;
    std::cout << "CHAT, stage 3\n";
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
        const auto [ok, text] = handle(store, std::move(raw));
        std::cout << text;
        if (text.empty() || text.back() != '\n') {
            std::cout << '\n';
        }
    }
    return 0;
}
