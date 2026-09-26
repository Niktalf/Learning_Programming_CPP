#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "owning_buffer.hpp"
#include "task.hpp"
#include "task_store.hpp"

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
    std::cout << "Commands:\n"
              << "  SUBMIT <queue> <body>   submit a task\n"
              << "  PEEK <queue>            look at the first one without taking it\n"
              << "  TAKE <queue>            take the first one\n"
              << "  QUEUES                  list the queues\n"
              << "  DEMO                    difference in captures in lambda\n"
              << "  HELP / QUIT\n";
}

// Демонстрация из постановки: один и тот же обход с двумя лямбдами.
// Разница возникает потому, что for_each_queue принимает функтор по значению.
void run_capture_demo(const TaskStore& store) {
    int by_reference = 0;
    store.for_each_queue([&by_reference](const std::string&,
                                         const TaskStore::Queue& tasks) {
        by_reference += static_cast<int>(tasks.size());
    });

    int by_value = 0;
    store.for_each_queue([by_value](const std::string&,
                                    const TaskStore::Queue& tasks) mutable {
        by_value += static_cast<int>(tasks.size());
    });

    std::cout << "захват по ссылке:   " << by_reference << '\n'
              << "захват по значению: " << by_value
              << "   (менялась копия внутри лямбды)\n";
}

}  // namespace

int main() {
    TaskStore store;
    TaskId next_id = 1;

    const OwningBuffer banner(std::string("TASKQ, этап 1"));
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
        } else if (command == "SUBMIT" && tokens.size() >= 3) {
            store.submit(tokens[1], Task(next_id, tokens[2]));
            std::cout << "OK id=" << next_id << '\n';
            ++next_id;
        } else if (command == "PEEK" && tokens.size() >= 2) {
            if (const Task* front = store.peek(tokens[1])) {
                std::cout << "id=" << front->id() << ' ' << front->payload() << '\n';
            } else {
                // Здесь и виден изъян интерфейса: сказать, очередь пуста
                // или её нет вовсе, мы не можем.
                std::cout << "(нет задачи: очередь пуста или не существует)\n";
            }
        } else if (command == "TAKE" && tokens.size() >= 2) {
            Task taken;
            if (store.take(tokens[1], taken)) {
                std::cout << "id=" << taken.id() << ' ' << taken.payload() << '\n';
            } else {
                std::cout << "(нет задачи: очередь пуста или не существует)\n";
            }
        } else if (command == "QUEUES") {
            store.for_each_queue([](const std::string& name,
                                    const TaskStore::Queue& tasks) {
                std::cout << name << "  (" << tasks.size() << ")\n";
            });
        } else if (command == "DEMO") {
            run_capture_demo(store);
        } else {
            std::cout << "(не разобрано; HELP покажет список команд)\n";
        }
    }
    return 0;
}
