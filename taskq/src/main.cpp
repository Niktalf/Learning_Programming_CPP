#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "taskq/owning_buffer.hpp"
#include "taskq/task.hpp"
#include "taskq/task_store.hpp"

namespace {

// Разбор строки самый прямолинейный: разбиение по пробелам с копированием,
// причём у команды SUBMIT тело задачи — весь остаток строки. На этапе 2 он
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
              << "  SUBMIT <queue> <body>   enqueue a task\n"
              << "  PEEK <queue>            show the first task without taking it\n"
              << "  TAKE <queue>            take the first task\n"
              << "  QUEUES                  list queues\n"
              << "  DEMO                    capture by reference vs by value\n"
              << "  HELP / QUIT\n";
}

// Демонстрация из постановки: один и тот же обход с двумя лямбдами.
// Разница возникает потому, что for_each_queue принимает функтор по значению.
void run_capture_demo(const taskq::TaskStore& store) {
    int by_reference = 0;
    store.for_each_queue([&by_reference](const std::string&,
                                         const taskq::TaskStore::Queue& tasks) {
        by_reference += static_cast<int>(tasks.size());
    });

    int by_value = 0;
    store.for_each_queue([by_value](const std::string&,
                                    const taskq::TaskStore::Queue& tasks) mutable {
        by_value += static_cast<int>(tasks.size());
    });

    std::cout << "captured by reference: " << by_reference << '\n'
              << "captured by value:     " << by_value
              << "   (the lambda changed its own copy)\n";
}

}  // namespace

int main() {
    const taskq::OwningBuffer banner(std::string("TASKQ, stage 1"));
    std::cout << banner.str() << '\n';
    print_help();

    taskq::TaskStore store;
    taskq::TaskId next_id = 1;
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
        } else if (command == "SUBMIT" && tokens.size() >= 3) {
            store.submit(tokens[1], taskq::Task(next_id, tokens[2]));
            std::cout << "OK id=" << next_id << '\n';
            ++next_id;
        } else if (command == "PEEK" && tokens.size() >= 2) {
            if (const taskq::Task* const front = store.peek(tokens[1])) {
                std::cout << "id=" << front->id() << ' ' << front->payload() << '\n';
            } else {
                // Здесь и виден изъян интерфейса: сказать, очередь пуста
                // или её нет вовсе, мы не можем.
                std::cout << "(nil: queue is empty or does not exist)\n";
            }
        } else if (command == "TAKE" && tokens.size() >= 2) {
            taskq::Task taken;   // выходной параметр take: объявляется до вызова
            if (store.take(tokens[1], taken)) {
                std::cout << "id=" << taken.id() << ' ' << taken.payload() << '\n';
            } else {
                std::cout << "(nil: queue is empty or does not exist)\n";
            }
        } else if (command == "QUEUES") {
            store.for_each_queue([](const std::string& name,
                                    const taskq::TaskStore::Queue& tasks) {
                std::cout << name << "  (" << tasks.size() << ")\n";
            });
        } else if (command == "DEMO") {
            run_capture_demo(store);
        } else {
            std::cout << "ERR unknown command or wrong arguments; HELP lists commands\n";
        }
    }
    return 0;
}
