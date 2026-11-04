// Домашнее задание этапа 3: простейший фаззер.
//
// Тысяча итераций каждого вида входа прогоняется через границу handle:
// случайные байты, обрезанные команды, очень длинные ключи, невалидный UTF-8,
// числа на краю диапазона. Проверяется, что граница ничего не выпускает
// наружу, на каждый вход есть ответ и учёт памяти хранилища не расходится
// с содержимым. Утечки и неопределённое поведение ловит сборка с
// санитайзерами — в ней этот тест и имеет смысл запускать.
//
// Что фаззер нашёл: INCR с шагом около INT64_MAX переполнял знаковое целое.
// Это неопределённое поведение: санитайзер остановил прогон, обычная сборка
// молча выдала бы отрицательное число. Исправление — проверка перед сложением
// и OverflowError; закрыто тестами Executor.IncrOverflowIsAnErrorNotUndefinedBehaviour
// и Handler.OverflowBecomesResponse. Остальные виды входа дефектов не выявили.

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "kvs/server.hpp"

namespace {

constexpr int iterations = 1000;

// Фиксированное зерно: упавший прогон воспроизводится один в один.
std::mt19937& rng() {
    static std::mt19937 engine(20261024);
    return engine;
}

int pick(int from, int to) {
    return std::uniform_int_distribution<int>(from, to)(rng());
}

std::string random_bytes(std::size_t max_size) {
    const std::size_t size = static_cast<std::size_t>(pick(0, static_cast<int>(max_size)));
    std::string bytes(size, '\0');
    for (char& c : bytes) {
        c = static_cast<char>(pick(0, 255));
    }
    return bytes;
}

const std::vector<std::string>& valid_commands() {
    static const std::vector<std::string> commands = {
        "SET key value", R"(SET k "two words")", "GET key", "DEL key", "EXISTS key",
        "TYPE key", "INCR counter", "INCR counter 5", "MSET a 1 b 2 c 3",
    };
    return commands;
}

// Сумма длин ключей и размеров значений должна совпадать с учётом хранилища.
bool memory_accounting_holds(const kvs::Store& store) {
    std::size_t total = 0;
    store.for_each([&total](const std::string& key, const kvs::Value& value) {
        total += key.size() + value.size();
    });
    return total == store.used_bytes();
}

void feed(kvs::Server& server, std::string line) {
    const std::string copy = line;   // для сообщения об ошибке: line переедет
    const kvs::Response response = server.handle(std::move(line));
    EXPECT_FALSE(response.text.empty()) << "no response to: " << copy;
}

}  // namespace

TEST(Fuzz, RandomBytes) {
    kvs::Server server;
    for (int i = 0; i < iterations; ++i) {
        feed(server, random_bytes(64));
    }
    EXPECT_TRUE(memory_accounting_holds(server.store()));
}

TEST(Fuzz, TruncatedCommands) {
    kvs::Server server;
    for (int i = 0; i < iterations; ++i) {
        const std::string& command =
            valid_commands()[static_cast<std::size_t>(pick(0, static_cast<int>(valid_commands().size()) - 1))];
        feed(server, command.substr(0, static_cast<std::size_t>(pick(0, static_cast<int>(command.size())))));
    }
    EXPECT_TRUE(memory_accounting_holds(server.store()));
}

TEST(Fuzz, VeryLongKeysAndValues) {
    kvs::Server server(kvs::Server::Options{1 << 20, std::nullopt});   // предел в мегабайт
    for (int i = 0; i < iterations; ++i) {
        const std::string key(static_cast<std::size_t>(pick(1, 20000)), 'k');
        const std::string value(static_cast<std::size_t>(pick(1, 20000)), 'v');
        feed(server, "SET " + key + std::to_string(i) + ' ' + value);
        feed(server, "GET " + key + std::to_string(i));
    }
    EXPECT_TRUE(memory_accounting_holds(server.store()));
    EXPECT_LE(server.store().used_bytes(), std::size_t{1} << 20);
}

TEST(Fuzz, InvalidUtf8) {
    kvs::Server server;
    const std::vector<std::string> broken = {"\xff", "\xc3", "\xe2\x82", "\xf0\x9f\x98",
                                             "\xc0\xaf", "\xed\xa0\x80"};
    for (int i = 0; i < iterations; ++i) {
        const std::string& bad = broken[static_cast<std::size_t>(pick(0, 5))];
        feed(server, "SET k" + bad + ' ' + bad + "value" + bad);
        feed(server, "GET k" + bad);
    }
    EXPECT_TRUE(memory_accounting_holds(server.store()));
}

TEST(Fuzz, NumbersAtTheEdge) {
    kvs::Server server;
    const std::vector<std::string> edges = {"9223372036854775807", "-9223372036854775808",
                                            "9223372036854775808", "-1", "0", "1",
                                            "99999999999999999999", "1e9", "0x10", "+5"};
    for (int i = 0; i < iterations; ++i) {
        feed(server, "INCR n " + edges[static_cast<std::size_t>(pick(0, 9))]);
    }
    EXPECT_TRUE(memory_accounting_holds(server.store()));
}
