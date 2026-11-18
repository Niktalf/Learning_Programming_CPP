#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "kvs/chained_table.hpp"
#include "kvs/hash.hpp"
#include "kvs/open_table.hpp"
#include "kvs/storage_engine.hpp"

namespace kvs {

// Этап 9: реализации движков. Этот заголовок включают точка сборки
// (src/engines.cpp, где инстанцирована фабрика), тесты движков и таблиц и
// замеры. Хранилище его не включает.

// Первая реализация — то, что было до этапа 8: std::unordered_map со
// стандартным хешем. Обход — через интерфейс корзин unordered_map
// (bucket_count, begin(n)), который стандарт даёт именно для таких случаев.
template <class Entry>
class StdEngine final : public IStorageEngine<Entry> {
    using Slot = typename IStorageEngine<Entry>::Slot;

    struct Hash {
        using is_transparent = void;
        [[nodiscard]] std::size_t operator()(std::string_view key) const noexcept {
            return std::hash<std::string_view>{}(key);
        }
    };
    using Map = std::unordered_map<std::string, Entry, Hash, std::equal_to<>>;

public:
    [[nodiscard]] std::string_view name() const noexcept override { return "std"; }
    [[nodiscard]] std::unique_ptr<IStorageEngine<Entry>> clone() const override {
        return std::make_unique<StdEngine>(*this);
    }

    [[nodiscard]] Entry* find(std::string_view key) noexcept override {
        const auto it = map_.find(key);
        return it == map_.end() ? nullptr : &it->second;
    }
    [[nodiscard]] const Entry* find(std::string_view key) const noexcept override {
        const auto it = map_.find(key);
        return it == map_.end() ? nullptr : &it->second;
    }

    bool put(std::string key, Entry entry) override {
        return map_.insert_or_assign(std::move(key), std::move(entry)).second;
    }

    bool erase(std::string_view key) noexcept override {
        const auto it = map_.find(key);
        if (it == map_.end()) {
            return false;
        }
        map_.erase(it);
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept override { return map_.size(); }
    [[nodiscard]] std::size_t bucket_count() const noexcept override { return map_.bucket_count(); }

    // Первая непустая корзина с номером не меньше bucket: find_if по
    // диапазону номеров корзин.
    [[nodiscard]] std::optional<Slot> first_from(std::size_t bucket) const noexcept override {
        const auto buckets = std::views::iota(std::min(bucket, map_.bucket_count()), map_.bucket_count());
        const auto found =
            std::ranges::find_if(buckets, [this](std::size_t b) { return map_.bucket_size(b) != 0; });
        return found == buckets.end() ? std::nullopt : std::optional<Slot>(Slot{*found, 0});
    }

    [[nodiscard]] std::optional<Slot> next(Slot slot) const noexcept override {
        if (slot.index + 1 < map_.bucket_size(slot.bucket)) {
            return Slot{slot.bucket, slot.index + 1};
        }
        return first_from(slot.bucket + 1);
    }

    [[nodiscard]] const std::string& key_at(Slot slot) const noexcept override { return at(slot)->first; }
    [[nodiscard]] const Entry& entry_at(Slot slot) const noexcept override { return at(slot)->second; }

private:
    [[nodiscard]] typename Map::const_local_iterator at(Slot slot) const noexcept {
        return std::next(map_.begin(slot.bucket), static_cast<std::ptrdiff_t>(slot.index));
    }

    Map map_;
};

// Собственные таблицы за тем же интерфейсом. Table — ChainedTable или
// OpenTable с ключом std::string; у них одинаковый набор операций, и обёртка
// одна на обе.
template <class Entry, class Table>
class TableEngine final : public IStorageEngine<Entry> {
    using Slot = typename IStorageEngine<Entry>::Slot;

public:
    TableEngine(std::string_view name, Table table) : name_(name), table_(std::move(table)) {}

    [[nodiscard]] std::string_view name() const noexcept override { return name_; }
    [[nodiscard]] std::unique_ptr<IStorageEngine<Entry>> clone() const override {
        return std::make_unique<TableEngine>(*this);
    }

    [[nodiscard]] Entry* find(std::string_view key) noexcept override { return table_.find(key); }
    [[nodiscard]] const Entry* find(std::string_view key) const noexcept override {
        return table_.find(key);
    }

    bool put(std::string key, Entry entry) override {
        return table_.insert_or_assign(std::move(key), std::move(entry));
    }

    bool erase(std::string_view key) noexcept override { return table_.erase(key); }

    [[nodiscard]] std::size_t size() const noexcept override { return table_.size(); }
    [[nodiscard]] std::size_t bucket_count() const noexcept override { return table_.bucket_count(); }

    [[nodiscard]] std::optional<Slot> first_from(std::size_t bucket) const noexcept override {
        return convert(table_.first_from(bucket));
    }
    [[nodiscard]] std::optional<Slot> next(Slot slot) const noexcept override {
        return convert(table_.next({slot.bucket, slot.index}));
    }
    [[nodiscard]] const std::string& key_at(Slot slot) const noexcept override {
        return table_.key_at({slot.bucket, slot.index});
    }
    [[nodiscard]] const Entry& entry_at(Slot slot) const noexcept override {
        return table_.value_at({slot.bucket, slot.index});
    }

    // Для гистограмм и замеров: сама таблица.
    [[nodiscard]] const Table& table() const noexcept { return table_; }

private:
    [[nodiscard]] static std::optional<Slot> convert(const auto& slot) noexcept {
        if (!slot) {
            return std::nullopt;
        }
        return Slot{slot->bucket, slot->index};
    }

    std::string_view name_;   // всегда строковый литерал из make_engine
    Table table_;
};


// Затравка FNV-1a — случайная на каждый движок: клиент её не знает и не
// может подобрать ключи с одинаковым хешем заранее.
[[nodiscard]] inline std::uint64_t random_seed() {
    static thread_local std::mt19937_64 generator{std::random_device{}()};
    return generator();
}

template <class Entry>
[[nodiscard]] std::unique_ptr<IStorageEngine<Entry>> make_engine(EngineKind kind) {
    using Chained = ChainedTable<std::string, Entry, Fnv1aHash>;
    using BadChained = ChainedTable<std::string, Entry, CharSumHash>;
    using Open = OpenTable<std::string, Entry, Fnv1aHash>;
    switch (kind) {
        case EngineKind::Std:
            break;
        case EngineKind::Hash:
            return std::make_unique<TableEngine<Entry, Chained>>("hash", Chained(Fnv1aHash{random_seed()}));
        case EngineKind::BadHash:
            return std::make_unique<TableEngine<Entry, BadChained>>("bad-hash", BadChained(CharSumHash{}));
        case EngineKind::Open:
            return std::make_unique<TableEngine<Entry, Open>>("open", Open(Fnv1aHash{random_seed()}));
    }
    return std::make_unique<StdEngine<Entry>>();
}


}  // namespace kvs
