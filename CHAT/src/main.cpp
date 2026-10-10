#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "chat_store.hpp"
#include "errors.hpp"
#include "handler.hpp"
#include "log.hpp"

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
                  << "  /logout name                user logged out\n"
                  << "  /invite #room name          add participant\n"
                  << "  /who #room                  live participants\n"
                  << "  HELP / QUIT\n";
    }
}  // namespace

int main() {
    try {
        AppendOnlyLog log("chat.log");
        std::cout << "CHAT, stage 4\n";
        print_help();

        std::string raw;
        while (std::getline(std::cin, raw)) {
            ChatStore store;
            const std::string_view view = raw;
            if (view == "QUIT") {
                break;
            }
            if (view == "HELP") {
                print_help();
                continue;
            }
            std::string record = raw;
            const auto [ok, text] = handle(store, std::move(raw));
            if (ok) {
                log.append(record);
            }
            std::cout << text;
            if (text.empty() || text.back() != '\n') {
                std::cout << '\n';
            }
        }
    } catch (const ChatError& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
