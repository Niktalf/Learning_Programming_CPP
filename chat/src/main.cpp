#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "chat/chat_store.hpp"
#include "chat/message.hpp"
#include "chat/owning_buffer.hpp"

namespace {

// Разбор строки самый прямолинейный: разбиение по пробелам с копированием,
// причём у команды MSG текст сообщения — весь остаток строки. На этапе 2 он
// будет переписан на std::string_view, и разницу в аллокациях мы измерим.
std::vector<std::string> split(const std::string& line, std::size_t tail_at) {
    std::vector<std::string> tokens;
    std::istringstream stream(line);
    std::string token;   // объявлен до цикла: в него читает operator>>
    while (tokens.size() + 1 < tail_at && stream >> token) {
        tokens.push_back(token);
    }
    std::string rest;
    std::getline(stream, rest);
    const std::size_t start = rest.find_first_not_of(' ');
    if (start != std::string::npos) {
        tokens.push_back(rest.substr(start));
    }
    return tokens;
}

void print_help() {
    std::cout << "Commands:\n"
              << "  JOIN <#room>          create a room\n"
              << "  MSG <#room> <text>    post a message\n"
              << "  HISTORY <#room>       show room history\n"
              << "  ROOMS                 list rooms\n"
              << "  DEMO                  capture by reference vs by value\n"
              << "  HELP / QUIT\n";
}

// Демонстрация из постановки: один и тот же обход с двумя лямбдами.
// Разница возникает потому, что for_each_room принимает функтор по значению.
void run_capture_demo(const chat::ChatStore& store) {
    int by_reference = 0;
    store.for_each_room([&by_reference](const std::string&,
                                        const chat::ChatStore::History& history) {
        by_reference += static_cast<int>(history.size());
    });

    int by_value = 0;
    store.for_each_room([by_value](const std::string&,
                                   const chat::ChatStore::History& history) mutable {
        by_value += static_cast<int>(history.size());
    });

    std::cout << "captured by reference: " << by_reference << '\n'
              << "captured by value:     " << by_value
              << "   (the lambda changed its own copy)\n";
}

}  // namespace

int main() {
    const chat::OwningBuffer banner(std::string("CHAT, stage 1"));
    std::cout << banner.str() << '\n';
    print_help();

    chat::ChatStore store;
    std::string line;   // объявлена до цикла: в неё читает std::getline
    while (std::getline(std::cin, line)) {
        const std::vector<std::string> tokens = split(line, 3);
        if (tokens.empty()) {
            continue;
        }
        const std::string& command = tokens.front();

        if (command == "QUIT") {
            break;
        } else if (command == "HELP") {
            print_help();
        } else if (command == "JOIN" && tokens.size() >= 2) {
            std::cout << (store.create_room(tokens[1]) ? "OK" : "ERR room already exists") << '\n';
        } else if (command == "MSG" && tokens.size() >= 3) {
            store.post(tokens[1], chat::Message("me", tokens[2]));
            std::cout << "OK\n";
        } else if (command == "HISTORY" && tokens.size() >= 2) {
            if (const chat::ChatStore::History* const history = store.history(tokens[1])) {
                for (const auto& message : *history) {
                    std::cout << '<' << message.author() << "> " << message.text() << '\n';
                }
            } else {
                std::cout << "ERR no such room\n";
            }
        } else if (command == "ROOMS") {
            store.for_each_room([](const std::string& room,
                                   const chat::ChatStore::History& history) {
                std::cout << room << "  (" << history.size() << ")\n";
            });
        } else if (command == "DEMO") {
            run_capture_demo(store);
        } else {
            std::cout << "ERR unknown command or wrong arguments; HELP lists commands\n";
        }
    }
    return 0;
}
