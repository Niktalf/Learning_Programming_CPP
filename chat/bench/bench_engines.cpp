// Этап 8, пункт 6: один и тот же замер на каждом движке хранилища чата.
//
//   ./build-release/chat_bench_engines
//
// Через хранилище — так, как с движком работают команды: N регистраций
// ников, поиск каждого в случайном порядке, N поисков отсутствующих ников,
// «текучка» — N выходов и входов новых ников, N комнат и поиск каждой.
// Ники — случайные строчные длиной 6–10 (nick_names.hpp).
//
// Результаты и честный вывод — docs/benchmarks.md.

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "bench_util.hpp"
#include "chat/chat_store.hpp"
#include "nick_names.hpp"

namespace {

void report(chat::EngineKind kind, std::size_t count) {
    // Три непересекающихся набора из одного распределения: основной,
    // отсутствующие и новые для текучки. Распределение длин у всех одно —
    // иначе отсутствующие ники с плохим хешем попадали бы в пустые корзины
    // (сумма кодов зависит от длины), и промах выглядел бы дешёвым.
    const auto all = chat::bench::random_nicks(3 * count, 1);
    const std::vector<std::string> nicks(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(count));
    const std::vector<std::string> absent(all.begin() + static_cast<std::ptrdiff_t>(count),
                                          all.begin() + static_cast<std::ptrdiff_t>(2 * count));
    const std::vector<std::string> fresh(all.begin() + static_cast<std::ptrdiff_t>(2 * count), all.end());
    std::vector<std::string> lookups = nicks;
    std::shuffle(lookups.begin(), lookups.end(), std::mt19937(4));
    std::vector<std::string> rooms;
    rooms.reserve(count);
    std::ranges::transform(nicks, std::back_inserter(rooms), [](const std::string& nick) { return "#" + nick; });
    const int rounds = count >= 1000000 ? 2 : 5;

    const auto make = [kind] { return chat::ChatStore(1, kind); };   // история на одно сообщение: меряются таблицы
    const auto register_all = [&nicks](chat::ChatStore& store) {
        for (const std::string& nick : nicks) {
            store.register_user(nick);
        }
    };
    const auto insert = chat::bench::measure(rounds, make, register_all);

    chat::ChatStore filled = make();
    register_all(filled);
    const auto hit = chat::bench::measure(rounds, [] { return 0; }, [&](int) {
        std::size_t found = 0;
        for (const std::string& nick : lookups) {
            found += filled.has_user(nick) ? 1 : 0;
        }
        chat::bench::keep(found);
    });
    const auto miss = chat::bench::measure(rounds, [] { return 0; }, [&](int) {
        std::size_t found = 0;
        for (const std::string& nick : absent) {
            found += filled.has_user(nick) ? 1 : 0;
        }
        chat::bench::keep(found);
    });
    const auto churn = chat::bench::measure(
        1, [&] { chat::ChatStore store = make(); register_all(store); return store; },
        [&](chat::ChatStore& store) {
            for (std::size_t i = 0; i < count; ++i) {
                store.release_user(nicks[i]);
                store.register_user(fresh[i]);
            }
        });
    const auto create = chat::bench::measure(rounds, make, [&rooms](chat::ChatStore& store) {
        for (const std::string& room : rooms) {
            store.create_room(room);
        }
    });
    chat::ChatStore with_rooms = make();
    for (const std::string& room : rooms) {
        with_rooms.create_room(room);
    }
    std::shuffle(rooms.begin(), rooms.end(), std::mt19937(5));
    const auto find_room = chat::bench::measure(rounds, [] { return 0; }, [&](int) {
        std::size_t found = 0;
        for (const std::string& room : rooms) {
            found += with_rooms.contains(room) ? 1 : 0;
        }
        chat::bench::keep(found);
    });
    std::cout << "| " << chat::name_of(kind) << " | " << count << " | " << insert.milliseconds << " | "
              << hit.milliseconds << " | " << miss.milliseconds << " | " << churn.milliseconds << " | "
              << create.milliseconds << " | " << find_room.milliseconds << " |\n";
}

}  // namespace

int main() {
    std::cout << "| engine | count | register, ms | find nick, ms | miss nick, ms | churn, ms | create room, ms |"
                 " find room, ms |\n|---|---|---|---|---|---|---|---|\n";
    for (const std::size_t count : {1000u, 100000u, 1000000u}) {
        for (const chat::EngineKind kind : {chat::EngineKind::Std, chat::EngineKind::Hash, chat::EngineKind::Open}) {
            report(kind, count);
        }
    }
    // Плохая хеш-функция — отдельно и на меньших размерах: на 10^6 её не
    // дождаться.
    for (const std::size_t count : {1000u, 10000u, 30000u}) {
        report(chat::EngineKind::Hash, count);
        report(chat::EngineKind::BadHash, count);
    }
    return 0;
}
