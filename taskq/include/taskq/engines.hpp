#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "taskq/chained_table.hpp"
#include "taskq/hash.hpp"
#include "taskq/open_table.hpp"
#include "taskq/task_index.hpp"

namespace taskq {

// Этап 9: реализации индекса по номеру. Этот заголовок включают точка
// сборки (src/engines.cpp), тесты движков и таблиц и замеры. Хранилище его
// не включает.

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
