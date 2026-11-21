#pragma once

#include <memory>
#include <string>

#include "kvs/commands.hpp"
#include "kvs/journal.hpp"
#include "kvs/request_cache.hpp"
#include "kvs/store.hpp"

namespace kvs {

// Сервер этапа 4: хранилище, кэш разобранных запросов и необязательный
// журнал. Владение однозначное: всё — поля, и всё освобождается
// деструктором Server в обратном порядке объявления; журнал с этапа 9 — по
// unique_ptr на порт, потому что его тип выбирает точка сборки.
//
// handle — единственная граница перехвата, как и на этапе 3: исключение
// любого вида превращается здесь в ответ клиенту. На этапе 15 handle будет
// вызывать сетевая сессия, на этапе 16 — обработчик HTTP-запроса.
class Server {
public:
    // Этап 9: все зависимости — снаружи. Хранилище приходит готовым (с
    // движком, пределом и часами), журнал — портами ILogSink и ILogSource
    // (journal.hpp). Сервер не знает ни файлов, ни путей, ни движков; собирает
    // его точка сборки make_server (compose.hpp). До этапа 9 конструктор
    // принимал Options с путём и сам открывал файл (docs/audit.md, запись 4).
    //
    // Если history задан, его записи сначала исполняются заново — так
    // хранилище восстанавливается после перезапуска; history нужен только
    // на время конструктора. Затем успешные изменения пишутся в log.
    //
    // Конструктор может бросить: запись не воспроизвелась. Деструктор Server
    // тогда не вызовется, но уже построенные поля разрушатся корректно.
    explicit Server(Store store = Store(), std::unique_ptr<ILogSink> log = nullptr,
                    const ILogSource* history = nullptr);

    [[nodiscard]] Response handle(std::string line);

    [[nodiscard]] const Store& store() const noexcept { return store_; }
    [[nodiscard]] const RequestCache& cache() const noexcept { return cache_; }

private:
    Store store_;
    RequestCache cache_;
    std::unique_ptr<ILogSink> log_;   // пусто — журнал не ведётся
};

}  // namespace kvs
