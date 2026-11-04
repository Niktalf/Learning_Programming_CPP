#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "chat/chat_store.hpp"
#include "chat/executor.hpp"
#include "chat/handler.hpp"
#include "chat/validate.hpp"

namespace {

void print_help() {
    std::cout << "Commands:\n"
              << "  /join <#room>                 join a room (creates it if needed)\n"
              << "  /leave <#room>                leave a room\n"
              << "  /msg <#room>[,#room...] <text>  post to one or several rooms, all or none;\n"
              << "                                :name: posts a reaction\n"
              << "  /who <#room>                  list room members\n"
              << "  /history <#room> [limit]      show the last messages\n"
              << "  /type <#room> <index>         kind of a message\n"
              << "  /react <#room> <index> [n]    add to a reaction counter\n"
              << "  /topic <#room> [\"topic\"]      show or set the topic\n"
              << "  /nick <nickname>              change your nickname\n"
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

// Ник задаётся первым аргументом: chat_cli alice. Сменить его можно командой
// /nick.
int main(int argc, char* argv[]) {
    if (argc > 2) {
        std::cerr << "usage: chat_cli [nickname]\n";
        return 2;
    }
    chat::Session session{argc == 2 ? std::string(argv[1]) : std::string("guest")};
    // Ник из командной строки проверяется теми же правилами, что и /nick:
    // иначе ник из одних пробелов попал бы в реестр в обход разбора.
    if (const auto problem = chat::nick_problem(session.nick); problem) {
        std::cerr << "bad nickname: " << *problem << '\n';
        return 2;
    }

    chat::ChatStore store;
    store.register_nick(session.nick);   // хранилище пустое, занят ник быть не может
    std::cout << "CHAT, stage 3, you are " << session.nick << '\n';
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
        // Вся обработка ошибок — внутри handle. Здесь нет ни одного try:
        // консольный клиент просто печатает ответ, каким бы он ни был.
        std::cout << chat::handle(store, session, std::move(raw)).text << '\n';
    }
    return 0;
}
