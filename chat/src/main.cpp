#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "chat/chat_store.hpp"
#include "chat/executor.hpp"
#include "chat/parser.hpp"

namespace {

void print_help() {
    std::cout << "Commands:\n"
              << "  /join <#room>                 join a room (creates it if needed)\n"
              << "  /leave <#room>                leave a room\n"
              << "  /msg <#room> <text>           post a message; :name: posts a reaction\n"
              << "  /who <#room>                  list room members\n"
              << "  /history <#room> [limit]      show the last messages\n"
              << "  /type <#room> <index>         kind of a message\n"
              << "  /react <#room> <index> [n]    add to a reaction counter\n"
              << "  /topic <#room> [\"topic\"]      show or set the topic\n"
              << "  DEMO                          capture by reference vs by value\n"
              << "  HELP / QUIT\n";
}

// Демонстрация этапа 1: один и тот же обход с двумя лямбдами.
void run_capture_demo(const chat::ChatStore& store) {
    int by_reference = 0;
    store.for_each_room([&by_reference](const std::string&, const chat::History& history) {
        by_reference += static_cast<int>(history.size());
    });

    int by_value = 0;
    store.for_each_room([by_value](const std::string&, const chat::History& history) mutable {
        by_value += static_cast<int>(history.size());
    });

    std::cout << "captured by reference: " << by_reference << '\n'
              << "captured by value:     " << by_value
              << "   (the lambda changed its own copy)\n";
}

}  // namespace

// Ник задаётся первым аргументом: chat_cli alice. Сменить его изнутри можно
// будет с этапа 6, когда появится команда /nick.
int main(int argc, char* argv[]) {
    const chat::Session session{argc > 1 ? std::string(argv[1]) : std::string("guest")};
    std::cout << "CHAT, stage 2, you are " << session.nick << '\n';
    print_help();

    chat::ChatStore store;
    std::string raw;   // объявлена до цикла: в неё читает std::getline
    while (std::getline(std::cin, raw)) {
        // Владелец строки живёт дольше результата разбора — в этом вся суть
        // архитектурного исправления этапа.
        const chat::InputLine line{std::move(raw)};

        const std::string_view view = line.view();
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

        if (const auto command = chat::parse(line); command) {
            std::cout << chat::execute(store, session, *command).text << '\n';
        } else {
            std::cout << "ERR unknown command or wrong arguments; HELP lists commands\n";
        }
    }
    return 0;
}
