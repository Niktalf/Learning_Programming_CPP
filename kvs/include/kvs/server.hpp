#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

#include "kvs/executor.hpp"
#include "kvs/log.hpp"
#include "kvs/request_cache.hpp"
#include "kvs/store.hpp"

namespace kvs {

// Сервер этапа 4: хранилище, кэш разобранных запросов и необязательный
// журнал. Владение здесь однозначное и без единого указателя: всё — поля,
// и всё освобождается деструктором Server в обратном порядке объявления.
//
// handle — единственная граница перехвата, как и на этапе 3: исключение
// любого вида превращается здесь в ответ клиенту. На этапе 15 handle будет
// вызывать сетевая сессия, на этапе 16 — обработчик HTTP-запроса.
class Server {
public:
    struct Options {
        std::optional<std::size_t> memory_limit;     // --maxmemory
        std::optional<std::filesystem::path> log;    // --log
    };

    Server() : Server(Options{}) {}

    // Если журнал задан и уже существует, его записи сначала исполняются
    // заново — так хранилище восстанавливается после перезапуска. Затем
    // журнал открывается на дозапись.
    //
    // Конструктор может бросить: журнал не открылся или не воспроизвёлся.
    // Деструктор Server тогда не вызовется, но уже построенные поля —
    // хранилище и кэш — разрушатся корректно. Это и есть ответ на вопрос
    // «что будет, если бросит конструктор».
    explicit Server(const Options& options);

    [[nodiscard]] Response handle(std::string line);

    [[nodiscard]] const Store& store() const noexcept { return store_; }
    [[nodiscard]] const RequestCache& cache() const noexcept { return cache_; }

private:
    Store store_;
    RequestCache cache_;
    std::optional<AppendOnlyLog> log_;   // пусто — журнал не ведётся
};

}  // namespace kvs
