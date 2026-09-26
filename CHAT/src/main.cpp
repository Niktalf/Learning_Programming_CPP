#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "chat_store.hpp"
#include "message.hpp"
#include "owning_buffer.hpp"

namespace {
std::vector<std::string> split(const std::string& line, const std::size_t tail_at) {
    std::vector<std::string> tokens;
    std::istringstream stream(line);
    std::string token;
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
    std::cout << "Команды:\n"
              << "  JOIN <#room>        create a room\n"
              << "  MSG <#room> <text>  send a message\n"
              << "  HISTORY <#room>     show history\n"
              << "  ROOMS               list rooms\n"
              << "  DEMO                difference in captures in the lambda\n"
              << "  HELP / QUIT\n";
}

// Демонстрация из постановки: один и тот же обход с двумя лямбдами.
// Разница возникает потому, что for_each_room принимает функтор по значению.
void run_capture_demo(const ChatStore& store) {
    int by_reference = 0;
    store.for_each_room([&by_reference](const std::string&,
                                        const ChatStore::History& history) {
        by_reference += static_cast<int>(history.size());
    });

    int by_value = 0;
    store.for_each_room([by_value](const std::string&,
                                   const ChatStore::History& history) mutable {
        by_value += static_cast<int>(history.size());
    });

    std::cout << "захват по ссылке:   " << by_reference << '\n'
              << "захват по значению: " << by_value
              << "   (менялась копия внутри лямбды)\n";
}

}  // namespace

int main() {
    ChatStore store;

    const OwningBuffer banner(std::string("CHAT, этап 1"));
    std::cout << banner.str() << '\n';
    print_help();

    std::string line;
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
            std::cout << (store.create_room(tokens[1]) ? "OK" : "(комната уже есть)") << '\n';
        } else if (command == "MSG" && tokens.size() >= 3) {
            store.post(tokens[1], Message("me", tokens[2]));
            std::cout << "OK\n";
        } else if (command == "HISTORY" && tokens.size() >= 2) {
            if (const ChatStore::History* history = store.history(tokens[1])) {
                for (const auto& message : *history) {
                    std::cout << '<' << message.author() << "> " << message.text() << '\n';
                }
            } else {
                std::cout << "(нет такой комнаты)\n";
            }
        } else if (command == "ROOMS") {
            store.for_each_room([](const std::string& room,
                                   const ChatStore::History& history) {
                std::cout << room << "  (" << history.size() << ")\n";
            });
        } else if (command == "DEMO") {
            run_capture_demo(store);
        } else {
            std::cout << "(не разобрано; HELP покажет список команд)\n";
        }
    }
    return 0;
}
