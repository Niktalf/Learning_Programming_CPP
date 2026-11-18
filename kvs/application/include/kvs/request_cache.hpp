#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "kvs/commands.hpp"
#include "kvs/input_line.hpp"
#include "kvs/lexer.hpp"
#include "kvs/registry.hpp"

namespace kvs {

// Разобранный запрос: строка, её токены и связанная команда — в одном объекте.
//
// Почему вместе. Аргументы связанной команды смотрят внутрь строки через
// string_view — это защита этапа 2, — а Rest у MSET смотрит ещё и в вектор
// токенов. Значит кэшировать команду отдельно от строки и токенов нельзя:
// она повиснет. Request владеет всеми тремя, и они живут и умирают
// одновременно.
//
// Объект нельзя ни копировать, ни перемещать — наследство InputLine. Поэтому
// он создаётся сразу в куче через std::make_shared и дальше передаётся только
// по указателю.
class Request {
public:
    // Бросает ParseError: неразобранный запрос не создаётся вовсе.
    explicit Request(std::string text)
        : line_(std::move(text)),
          tokens_(tokenize(line_.view())),
          command_(commands().bind(tokens_, line_.view())) {}

    Request(const Request&) = delete;
    Request& operator=(const Request&) = delete;

    [[nodiscard]] const CommandRegistry<Store>::Bound& command() const noexcept { return command_; }
    [[nodiscard]] std::string_view text() const noexcept { return line_.view(); }

private:
    // Порядок объявления — порядок инициализации, и он существен: каждое
    // следующее поле смотрит внутрь предыдущих.
    InputLine line_;
    Tokens tokens_;
    CommandRegistry<Store>::Bound command_;
};

// Кэш разобранных запросов на weak_ptr.
//
// Кэш наблюдает, но не владеет. Владелец запроса — тот, кто его исполняет:
// Server::handle держит shared_ptr, пока команда выполняется. Пока запрос
// жив, повторный разбор той же строки не нужен — кэш отдаст готовый объект.
// Как только последний владелец отпустил запрос, объект уничтожается, а
// запись в кэше становится истёкшей: lock() вернёт пустой указатель.
//
// В консольном клиенте запрос живёт ровно одну команду, и попаданий нет.
// На этапе 15 одновременно исполняются запросы многих сессий, и одинаковые
// строки — GET горячего ключа — будут разбираться один раз.
//
// С shared_ptr вместо weak_ptr кэш удерживал бы каждый запрос вечно и
// превратился бы в утечку — логическую, которую санитайзер не увидит.
class RequestCache {
public:
    // Порог, после которого get сам вычищает истёкшие записи.
    explicit RequestCache(std::size_t purge_threshold = 1024) noexcept
        : purge_threshold_(purge_threshold) {}

    // Живой запрос из кэша или новый разбор. Бросает ParseError.
    [[nodiscard]] std::shared_ptr<const Request> get(const std::string& text);

    // Удаляет истёкшие записи и возвращает, сколько удалено. Без этого
    // таблица растёт неограниченно: объекты освобождаются, а строки-ключи
    // остаются.
    std::size_t purge();

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] std::size_t hits() const noexcept { return hits_; }

private:
    std::map<std::string, std::weak_ptr<const Request>, std::less<>> entries_;
    std::size_t purge_threshold_;
    std::size_t hits_ = 0;
};

}  // namespace kvs
