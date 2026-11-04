#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "chat/server.hpp"
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

struct Arguments {
    std::string nick = "guest";
    std::optional<std::string> log;
};

// Разбор аргументов: необязательный ник и --log=PATH в любом порядке.
// Неизвестный ключ — отказ запуска, а не молчаливое игнорирование.
[[nodiscard]] std::optional<Arguments> parse_arguments(int argc, char* argv[]) {
    constexpr std::string_view log_prefix = "--log=";
    Arguments arguments;
    bool nick_given = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument.substr(0, log_prefix.size()) == log_prefix &&
            argument.size() > log_prefix.size()) {
            arguments.log = std::string(argument.substr(log_prefix.size()));
        } else if (!nick_given && argument.substr(0, 2) != "--") {
            arguments.nick = std::string(argument);
            nick_given = true;
        } else {
            return std::nullopt;
        }
    }
    return arguments;
}

}  // namespace

// Ник задаётся аргументом: chat_cli alice. Сменить его можно командой /nick.
int main(int argc, char* argv[]) {
    const auto arguments = parse_arguments(argc, argv);
    if (!arguments) {
        std::cerr << "usage: chat_cli [nickname] [--log=PATH]\n";
        return 2;
    }
    // Ник из командной строки проверяется теми же правилами, что и /nick:
    // иначе ник из одних пробелов попал бы в реестр в обход разбора.
    if (const auto problem = chat::nick_problem(arguments->nick); problem) {
        std::cerr << "bad nickname: " << *problem << '\n';
        return 2;
    }

    // Запуск — отдельная граница, не командная: журнал не открылся, и
    // работать дальше нельзя. Сервер строится в optional через emplace, без
    // кучи, — только затем, чтобы конструктор оказался внутри try.
    std::optional<chat::Server> server;
    try {
        if (arguments->log) {
            server.emplace(*arguments->log);
        } else {
            server.emplace();
        }
    } catch (const std::exception& e) {
        std::cerr << "cannot start: " << e.what() << '\n';
        return 1;
    }
    // Реестр пуст, ник не может быть занят.
    chat::Session session = server->login(arguments->nick);

    std::cout << "CHAT, stage 4, you are " << session.nick << '\n';
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
        // Вся обработка ошибок команд — внутри handle. Здесь нет ни одного
        // try: консольный клиент просто печатает ответ, каким бы он ни был.
        std::cout << server->handle(session, std::move(raw)).text << '\n';
    }
    server->logout(session);
    return 0;
}
