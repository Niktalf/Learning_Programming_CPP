#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <random>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "taskq/chained_table.hpp"
#include "taskq/hash.hpp"
#include "taskq/open_table.hpp"
#include "taskq/task.hpp"

namespace taskq {

// Этап 8: индекс задач по номеру — за интерфейсом движка.
//
// Хранилище задач (BasicTaskStore) — это и есть ITaskStore постановки:
// постановка, выборка по приоритету, поиск по номеру, смена состояния,
// обход. Выборка по приоритету — деревья очередей (std::map по TaskKey,
// этап 7), и они не хеш-таблицы: у них нет второй реализации, которую
// стоило бы сравнивать. Поиск по номеру — единственная хеш-таблица
// хранилища, и именно её постановка велит заменить собственной. Поэтому за
// интерфейсом — она: всё хранилище работает с номерами задач только через
// ITaskIndex, и одни и те же тесты проходят на каждой реализации. Выбор —
// параметр запуска (--engine, TASKQ_ENGINE).
//
// Location — параметр шаблона: что записано для задачи, знает хранилище
// (очередь и позиция в её дереве или в индексе отложенных).
//
// Интерфейс виртуальный, а не шаблонный параметр хранилища: движок
// выбирается при запуске, а не при компиляции. Цена — косвенный вызов на
// каждый поиск; она измерена в docs/benchmarks.md.
template <class Location>
class ITaskIndex {
public:
    virtual ~ITaskIndex() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual Location* find(TaskId id) noexcept = 0;
    [[nodiscard]] virtual const Location* find(TaskId id) const noexcept = 0;
    // Новая запись. false — номер уже есть, индекс не изменён. Сильная
    // гарантия: при броске индекс прежний.
    virtual bool insert(TaskId id, Location location) = 0;
    virtual bool erase(TaskId id) noexcept = 0;
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;
    // Место под count записей без рехеширования.
    virtual void reserve(std::size_t count) = 0;

protected:
    ITaskIndex() = default;
    ITaskIndex(const ITaskIndex&) = default;
    ITaskIndex& operator=(const ITaskIndex&) = default;
};

// Первая реализация — код этапа 7, переехавший за интерфейс:
// std::unordered_map со стандартным хешем.
template <class Location>
class StdTaskIndex final : public ITaskIndex<Location> {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "std"; }
    [[nodiscard]] Location* find(TaskId id) noexcept override {
        const auto it = map_.find(id);
        return it == map_.end() ? nullptr : &it->second;
    }
    [[nodiscard]] const Location* find(TaskId id) const noexcept override {
        const auto it = map_.find(id);
        return it == map_.end() ? nullptr : &it->second;
    }
    bool insert(TaskId id, Location location) override { return map_.try_emplace(id, std::move(location)).second; }
    bool erase(TaskId id) noexcept override { return map_.erase(id) != 0; }
    [[nodiscard]] std::size_t size() const noexcept override { return map_.size(); }
    void reserve(std::size_t count) override { map_.reserve(count); }

private:
    std::unordered_map<TaskId, Location> map_;
};

// Реализации на собственных таблицах: Table — шаблон таблицы
// (ChainedTable или OpenTable), Hash — хеш-функция номера.
template <class Location, template <class, class, class, class> class Table, class Hash>
class TableTaskIndex final : public ITaskIndex<Location> {
public:
    TableTaskIndex(std::string_view name, Hash hash) : name_(name), table_(hash) {}

    [[nodiscard]] std::string_view name() const noexcept override { return name_; }
    [[nodiscard]] Location* find(TaskId id) noexcept override { return table_.find(id); }
    [[nodiscard]] const Location* find(TaskId id) const noexcept override { return table_.find(id); }
    bool insert(TaskId id, Location location) override {
        if (table_.find(id) != nullptr) {
            return false;
        }
        table_.insert_new(id, std::move(location));
        return true;
    }
    bool erase(TaskId id) noexcept override { return table_.erase(id); }
    [[nodiscard]] std::size_t size() const noexcept override { return table_.size(); }
    void reserve(std::size_t count) override { table_.reserve(count); }

private:
    std::string_view name_;   // строковый литерал из make_task_index
    Table<TaskId, Location, Hash, std::equal_to<>> table_;
};

enum class EngineKind { Std, Hash, BadHash, Open };

inline constexpr std::array<EngineKind, 4> all_engine_kinds = {EngineKind::Std, EngineKind::Hash,
                                                               EngineKind::BadHash, EngineKind::Open};

[[nodiscard]] constexpr std::string_view name_of(EngineKind kind) noexcept {
    switch (kind) {
        case EngineKind::Std:
            return "std";
        case EngineKind::Hash:
            return "hash";
        case EngineKind::BadHash:
            return "bad-hash";
        case EngineKind::Open:
            return "open";
    }
    return "std";
}

// Поиск по проекции: find сравнивает с name не сам элемент, а name_of от
// него.
[[nodiscard]] inline std::optional<EngineKind> parse_engine_kind(std::string_view name) noexcept {
    const auto found = std::ranges::find(all_engine_kinds, name, name_of);
    return found == all_engine_kinds.end() ? std::nullopt : std::optional<EngineKind>(*found);
}

// Движок по умолчанию — из переменной окружения TASKQ_ENGINE, иначе std.
// Так весь набор тестов прогоняется на каждом движке без единой правки:
// ctest запускает его четыре раза с разным TASKQ_ENGINE (CMakeLists.txt).
// Неизвестное имя — std: консольный клиент проверяет --engine сам.
[[nodiscard]] inline EngineKind default_engine_kind() noexcept {
    const char* const value = std::getenv("TASKQ_ENGINE");   // NOLINT: окружение читается до потоков
    return value == nullptr ? EngineKind::Std : parse_engine_kind(value).value_or(EngineKind::Std);
}

// Случайная затравка хеша на процесс (docs/hashing.md).
[[nodiscard]] inline std::uint64_t random_seed() {
    static const std::uint64_t seed = [] {
        std::random_device device;
        return (static_cast<std::uint64_t>(device()) << 32) ^ device();
    }();
    return seed;
}

template <class Location>
[[nodiscard]] std::unique_ptr<ITaskIndex<Location>> make_task_index(EngineKind kind) {
    switch (kind) {
        case EngineKind::Hash:
            return std::make_unique<TableTaskIndex<Location, ChainedTable, Fnv1aHash>>("hash",
                                                                                       Fnv1aHash{random_seed()});
        case EngineKind::BadHash:
            return std::make_unique<TableTaskIndex<Location, ChainedTable, ModuloHash>>("bad-hash", ModuloHash{});
        case EngineKind::Open:
            return std::make_unique<TableTaskIndex<Location, OpenTable, Fnv1aHash>>("open",
                                                                                    Fnv1aHash{random_seed()});
        case EngineKind::Std:
            break;
    }
    return std::make_unique<StdTaskIndex<Location>>();
}

}  // namespace taskq
