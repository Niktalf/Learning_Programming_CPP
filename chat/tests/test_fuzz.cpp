// Домашнее задание этапа 3: простейший фаззер.
//
// Тысяча итераций каждого вида входа прогоняется через границу handle:
// случайные байты, обрезанные команды, длинные имена и строки, невалидный
// UTF-8, числа на краю диапазона, списки комнат с повторами и пропусками.
// Проверяется не только то, что граница ничего не выпускает наружу, но и
// инварианты хранилища: имена комнат и ники корректны, в историю не попал
// невалидный UTF-8 и управляющие символы, ни одна строка истории не длиннее
// предела. Утечки и неопределённое поведение ловит сборка с санитайзерами —
// в ней этот тест и имеет смысл запускать.
//
// Что фаззер нашёл на версии без проверок из chat/validate.hpp (каждая
// находка закрыта тестом в tests/test_validate.cpp или tests/test_executor.cpp):
//
//   1. Имя комнаты в кавычках — /join "# " или /join "#a b" — проходило
//      проверку «начинается с #» и создавало комнату с пробелами в имени;
//      имена в сотню символов проходили тоже.
//   2. Ник из одних пробелов — /nick "   " — попадал в реестр, как и ник
//      длиной в сотню символов.
//   3. Обрезанный UTF-8 (\xd0 в конце строки) ложился в историю, и /history
//      отдавал клиенту невалидный текст.
//   4. Управляющие символы: ESC-последовательность в /msg уходила в
//      терминалы остальных участников.
//   5. Строка в мегабайт целиком ложилась в историю.
//   6. /react с шагом около INT64_MAX переполнял знаковое целое —
//      неопределённое поведение, на котором санитайзер остановил прогон.
//
// Отказ в разборе и отказ в исполнении — нормальные ответы. Дефект — это
// только нарушение инварианта или исключение, вылетевшее из handle.

#include <gtest/gtest.h>

#include <cstddef>
#include <random>
#include <string>
#include <vector>

#include "chat/chat_store.hpp"
#include "chat/server.hpp"
#include "chat/validate.hpp"

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
        "/join #general", "/leave #general", "/msg #general hello there", "/msg #a,#b both",
        "/who #general", "/history #general 3", "/type #general 0", "/react #general 2 2",
        R"(/topic #general "new topic")", "/nick fuzzer2", "/msg #general :fire:",
    };
    return commands;
}

// Инварианты хранилища. Каждый провал — дефект, а не отказ.
void check_invariants(const chat::ChatStore& store) {
    store.for_each_room([&store](const std::string& room, const chat::History& history) {
        EXPECT_FALSE(chat::room_name_problem(room).has_value()) << "bad room: " << room;
        // Результат members — сначала в переменную. Цикл по *store.members(room)
        // обходил бы временный optional, уничтоженный до начала цикла: время
        // жизни временного объекта в заголовке range-for продлевается только
        // с C++23. Санитайзер поймал ровно это — висячую ссылку этапа 2.
        const auto members = store.members(room);
        for (const auto& user : *members) {
            EXPECT_FALSE(chat::nick_problem(user->nick()).has_value()) << "bad nick: " << user->nick();
        }
        for (const chat::MessagePtr& message : history) {
            const std::string text = message->to_string();
            EXPECT_TRUE(chat::is_valid_utf8(text)) << "invalid UTF-8 in " << room;
            EXPECT_LE(text.size(), chat::max_line_length) << "huge message in " << room;
            if (const auto* const body = dynamic_cast<const chat::TextMessage*>(message.get())) {
                EXPECT_FALSE(chat::text_problem(body->text()).has_value())
                    << "bad text in " << room;
            }
        }
    });
}

// Площадка: две комнаты, в которых сессия уже состоит, и несколько сообщений.
struct Arena {
    chat::Server server;
    chat::Session session = server.login("fuzzer");
    const chat::ChatStore& store = server.store();

    Arena() {
        for (const char* const line : {"/join #general", "/join #a", "/join #b",
                                       "/msg #general hello", "/msg #general :fire:"}) {
            static_cast<void>(server.handle(session, line));
        }
    }

    void feed(const std::string& line) {
        const chat::Response response = server.handle(session, line);
        EXPECT_FALSE(response.text.empty()) << "no response to: " << line;
    }
};

}  // namespace

TEST(Fuzz, RandomBytes) {
    Arena arena;
    for (int i = 0; i < iterations; ++i) {
        // Половина строк начинается с настоящей команды, чтобы мусор доходил
        // дальше первой проверки.
        const std::string prefix = pick(0, 1) == 0 ? std::string() : "/msg #general ";
        arena.feed(prefix + random_bytes(64));
    }
    check_invariants(arena.store);
}

TEST(Fuzz, TruncatedCommands) {
    Arena arena;
    for (int i = 0; i < iterations; ++i) {
        const std::string& command = any_of(valid_commands());
        arena.feed(command.substr(0, static_cast<std::size_t>(pick(0, static_cast<int>(command.size())))));
    }
    check_invariants(arena.store);
}

TEST(Fuzz, LongNamesAndLines) {
    Arena arena;
    for (int i = 0; i < iterations / 10; ++i) {
        const std::string name(static_cast<std::size_t>(pick(1, 100)), 'r');
        arena.feed("/join #" + name);
        arena.feed("/nick " + name);
        arena.feed("/msg #general " + std::string(static_cast<std::size_t>(pick(1, 1 << 20)), 'x'));
    }
    check_invariants(arena.store);
}

TEST(Fuzz, QuotedNamesWithSpaces) {
    Arena arena;
    const std::vector<std::string> names = {"# ", "#a b", "#", "  ", "\t", "#\xd0", "#общий"};
    for (int i = 0; i < iterations; ++i) {
        const std::string& name = any_of(names);
        arena.feed("/join \"" + name + "\"");
        arena.feed("/nick \"" + name + "\"");
    }
    check_invariants(arena.store);
}

TEST(Fuzz, InvalidUtf8AndControlCharacters) {
    Arena arena;
    const std::vector<std::string> broken = {"\xff",     "\xd0",         "\xe2\x82", "\xf0\x9f\x98",
                                             "\xc0\xaf", "\xed\xa0\x80", "\x1b[2J",  std::string(1, '\0'),
                                             "\r",       "\x7f"};
    for (int i = 0; i < iterations; ++i) {
        const std::string& bad = any_of(broken);
        arena.feed("/msg #general text" + bad);
        arena.feed("/topic #general \"" + bad + "\"");
    }
    check_invariants(arena.store);
}

TEST(Fuzz, NumbersAtTheEdge) {
    Arena arena;
    const std::vector<std::string> edges = {"9223372036854775807", "-9223372036854775808",
                                            "9223372036854775808", "-1", "0", "1",
                                            "99999999999999999999", "1e9", "0x10", "+5"};
    for (int i = 0; i < iterations; ++i) {
        const std::string& number = any_of(edges);
        arena.feed("/react #general 2 " + number);   // 2 — реакция :fire:
        arena.feed("/history #general " + number);
        arena.feed("/type #general " + number);
    }
    check_invariants(arena.store);
}

TEST(Fuzz, RoomListsWithRepeatsAndGaps) {
    Arena arena;
    const std::vector<std::string> rooms = {"#a", "#b", "#general", "#absent", "", "a", "#a b"};
    for (int i = 0; i < iterations; ++i) {
        std::string list = any_of(rooms);
        for (int k = pick(0, 4); k > 0; --k) {
            list += ',' + any_of(rooms);
        }
        arena.feed("/msg " + list + " hello");
    }
    check_invariants(arena.store);
}
