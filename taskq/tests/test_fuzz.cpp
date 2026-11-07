// Домашнее задание этапа 3: простейший фаззер.
//
// Тысяча итераций каждого вида входа прогоняется через границу handle:
// случайные байты, обрезанные команды, огромные тела и длинные имена, имена
// очередей из пробелов, приоритеты за пределами диапазона, номера
// несуществующих задач, числа на краю диапазона, пакеты с неизвестной
// очередью в середине. Проверяется не только то, что граница ничего не
// выпускает наружу, но и инварианты сервиса: имена очередей корректны, тела
// задач — валидный UTF-8 не длиннее предела, каждая задача в очереди имеет
// состояние pending именно в этой очереди, и других ожидающих задач нет.
// Утечки и неопределённое поведение ловит сборка с санитайзерами.
//
// Что фаззер нашёл на версии с проверками этапа 2 (каждая находка закрыта
// тестом в tests/test_validate.cpp или tests/test_commands.cpp, до этапа 6 —
// test_executor.cpp):
//
//   1. Имя очереди из одних пробелов — CREATE "   " — создавало очередь,
//      в которую потом невозможно было адресовать задачу без кавычек;
//      имена с пробелами по краям и табуляцией — тоже.
//   2. Тело в мегабайт целиком ложилось в очередь.
//   3. Обрезанный UTF-8 в теле задачи ложился в очередь и уходил воркеру.
//   4. BUMP с шагом около INT64_MAX переполнял знаковое целое —
//      неопределённое поведение, на котором санитайзер остановил прогон.
//
// Отрицательный приоритет, приоритет больше 9 и ACK несуществующей задачи —
// типовые находки по методичке — дефектами не оказались: разбор этапа 2
// отвергал приоритеты, а ACK неизвестного номера даёт UnknownTaskError.
// Фаззер закрепляет это: такие входы остаются в прогоне.

#include <gtest/gtest.h>

#include <cstddef>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "taskq/service.hpp"
#include "taskq/handler.hpp"
#include "taskq/validate.hpp"

namespace {

constexpr int iterations = 1000;

// Фиксированное зерно: упавший прогон воспроизводится один в один.
std::mt19937& rng() {
    static std::mt19937 engine(20261025);
    return engine;
}

int pick(int from, int to) {
    return std::uniform_int_distribution<int>(from, to)(rng());
}

template <class T>
const T& any_of(const std::vector<T>& items) {
    return items[static_cast<std::size_t>(pick(0, static_cast<int>(items.size()) - 1))];
}

std::string random_bytes(std::size_t max_size) {
    const auto size = static_cast<std::size_t>(pick(0, static_cast<int>(max_size)));
    std::string bytes(size, '\0');
    for (char& c : bytes) {
        c = static_cast<char>(pick(0, 255));
    }
    return bytes;
}

const std::vector<std::string>& valid_commands() {
    static const std::vector<std::string> commands = {
        "CREATE build",  "SUBMIT build make all", "SUBMIT build prio=5 num:3", "POLL build",
        "ACK 1",         "FAIL 2",                "RELEASE 3",                 "STATUS 1",
        "TYPE 2",        "BUMP 3 5",              R"(SUBMITBATCH build "a b" deploy c)",
    };
    return commands;
}

// Инварианты сервиса. Каждый провал — дефект, а не отказ.
void check_invariants(taskq::Service& service) {
    std::set<taskq::TaskId> queued;
    service.store().for_each_queue([&](const std::string& queue, const taskq::Queue& tasks) {
        EXPECT_FALSE(taskq::queue_name_problem(queue).has_value()) << "bad queue: [" << queue << ']';
        for (const taskq::Task& task : tasks) {
            const std::string body = task.to_string();
            EXPECT_TRUE(taskq::is_valid_utf8(body)) << "invalid UTF-8 in task " << task.id();
            EXPECT_LE(body.size(), taskq::max_line_length) << "huge task " << task.id();
            EXPECT_TRUE(queued.insert(task.id()).second) << "task queued twice: " << task.id();
            EXPECT_EQ(taskq::handle(service, "STATUS " + std::to_string(task.id())).text,
                      "pending in " + queue);
        }
    });
    // И обратно: каждая задача в состоянии pending лежит в очереди. Номера
    // выдаются подряд, поэтому перебор до первого неизвестного номера
    // проходит по всем задачам.
    for (taskq::TaskId id = 1;; ++id) {
        const taskq::Response status = taskq::handle(service, "STATUS " + std::to_string(id));
        if (!status.ok) {
            break;
        }
        if (status.text.rfind("pending", 0) == 0) {
            EXPECT_EQ(queued.count(id), 1u) << "pending task " << id << " is in no queue";
        }
    }
}

// Площадка: две очереди и несколько задач в разных состояниях.
struct Arena {
    taskq::Service service;

    Arena() {
        for (const char* const line :
             {"CREATE build", "CREATE deploy", "SUBMIT build make all", "SUBMIT build num:40",
              "SUBMIT deploy ship", "POLL build"}) {
            static_cast<void>(taskq::handle(service, line));
        }
    }

    void feed(const std::string& line) {
        const taskq::Response response = taskq::handle(service, line);
        EXPECT_FALSE(response.text.empty()) << "no response to: " << line;
    }
};

}  // namespace

TEST(Fuzz, RandomBytes) {
    Arena arena;
    for (int i = 0; i < iterations; ++i) {
        // Половина строк начинается с настоящей команды, чтобы мусор доходил
        // дальше первой проверки.
        const std::string prefix = pick(0, 1) == 0 ? std::string() : "SUBMIT build ";
        arena.feed(prefix + random_bytes(64));
    }
    check_invariants(arena.service);
}

TEST(Fuzz, TruncatedCommands) {
    Arena arena;
    for (int i = 0; i < iterations; ++i) {
        const std::string& command = any_of(valid_commands());
        arena.feed(command.substr(0, static_cast<std::size_t>(pick(0, static_cast<int>(command.size())))));
    }
    check_invariants(arena.service);
}

TEST(Fuzz, HugeBodiesAndLongNames) {
    Arena arena;
    for (int i = 0; i < iterations / 10; ++i) {
        const std::string name(static_cast<std::size_t>(pick(1, 200)), 'q');
        arena.feed("CREATE " + name);
        arena.feed("SUBMIT build " + std::string(static_cast<std::size_t>(pick(1, 1 << 20)), 'x'));
    }
    check_invariants(arena.service);
}

TEST(Fuzz, QueueNamesOfSpacesAndControls) {
    Arena arena;
    const std::vector<std::string> names = {"   ", " build", "build ", "\t", "a\tb", "\x1b[2J", ""};
    for (int i = 0; i < iterations; ++i) {
        const std::string quoted = '"' + any_of(names) + '"';
        arena.feed("CREATE " + quoted);
        arena.feed("SUBMIT " + quoted + " x");
    }
    check_invariants(arena.service);
}

TEST(Fuzz, InvalidUtf8InBodies) {
    Arena arena;
    const std::vector<std::string> broken = {"\xff", "\xd0", "\xe2\x82", "\xf0\x9f\x98", "\xc0\xaf",
                                             "\xed\xa0\x80"};
    for (int i = 0; i < iterations; ++i) {
        arena.feed("SUBMIT build make" + any_of(broken));
    }
    check_invariants(arena.service);
}

TEST(Fuzz, PrioritiesAndIdsOutOfRange) {
    Arena arena;
    const std::vector<std::string> priorities = {"-1", "10", "99999999999", "", "5x", "0", "9"};
    const std::vector<std::string> verbs = {"ACK", "FAIL", "RELEASE", "STATUS", "TYPE", "BUMP"};
    for (int i = 0; i < iterations; ++i) {
        arena.feed("SUBMIT build prio=" + any_of(priorities) + " x");
        arena.feed(any_of(verbs) + ' ' + std::to_string(pick(0, 5000)));
    }
    check_invariants(arena.service);
}

TEST(Fuzz, NumbersAtTheEdge) {
    Arena arena;
    const std::vector<std::string> edges = {"9223372036854775807", "-9223372036854775808",
                                            "9223372036854775808", "-1", "0", "1",
                                            "99999999999999999999", "1e9", "0x10", "+5"};
    for (int i = 0; i < iterations; ++i) {
        // Задача 2 — num:40 в очереди build, единственная числовая.
        arena.feed("BUMP 2 " + any_of(edges));
        arena.feed("SUBMIT build num:" + any_of(edges));
        arena.feed("LIST build limit=" + any_of(edges));   // этап 5: предел LIST на краю
    }
    check_invariants(arena.service);
}

TEST(Fuzz, BatchesWithGaps) {
    Arena arena;
    const std::vector<std::string> queues = {"build", "deploy", "absent", "\"   \""};
    for (int i = 0; i < iterations; ++i) {
        std::string line = "SUBMITBATCH";
        for (int k = pick(0, 5); k > 0; --k) {
            line += ' ' + any_of(queues) + " item" + std::to_string(k);
        }
        arena.feed(line);
    }
    check_invariants(arena.service);
}
