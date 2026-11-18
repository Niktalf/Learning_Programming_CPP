#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iterator>
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

namespace kvs {

// Этап 8: хранилище за интерфейсом движка.
//
// Движок — таблица «ключ → запись»: put, get (find), erase, contains, size и
// обход. Всё остальное — учёт объёма, версии, предел памяти, списки,
// диапазонный индекс — остаётся в BasicStore и одинаково для всех движков.
// Поэтому одни и те же тесты проходят на каждом: тесты знают только
// BasicStore, а движок выбирается при его создании.
//
// Обход — по позициям: корзина и номер в ней. Позиция — не итератор
// конкретного контейнера, а два числа, которые понимает любая хеш-таблица,
// и на них стоят и итератор хранилища этапа 7, и SCAN с курсором: курсор
// SCAN — номер корзины, как в Redis.
//
// Виртуальный интерфейс, а не шаблонный параметр: движок выбирается при
// запуске (--engine), а не при компиляции. Цена — косвенный вызов на каждую
// операцию; в docs/benchmarks.md видно, что она мала по сравнению с самой
// таблицей.
template <class Entry>
class IStorageEngine {
public:
    struct Slot {
        std::size_t bucket = 0;
        std::size_t index = 0;
        friend bool operator==(const Slot&, const Slot&) = default;
    };

    virtual ~IStorageEngine() = default;

    // Имя движка — для --engine и для отчётов.
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    // Глубокая копия: на ней стоит copy-and-swap у MSET.
    [[nodiscard]] virtual std::unique_ptr<IStorageEngine> clone() const = 0;

    // Указатель — наблюдатель, не владелец; пустой — ключа нет. Действителен
    // до следующей вставки: у открытой адресации вставка может перенести
    // все элементы.
    [[nodiscard]] virtual Entry* find(std::string_view key) noexcept = 0;
    [[nodiscard]] virtual const Entry* find(std::string_view key) const noexcept = 0;

    // Вставка или замена с сильной гарантией. true — ключ новый.
    virtual bool put(std::string key, Entry entry) = 0;

    virtual bool erase(std::string_view key) noexcept = 0;

    [[nodiscard]] virtual std::size_t size() const noexcept = 0;

    [[nodiscard]] bool contains(std::string_view key) const noexcept { return find(key) != nullptr; }

    // --- Обход по позициям -----------------------------------------------
    [[nodiscard]] virtual std::size_t bucket_count() const noexcept = 0;
    [[nodiscard]] virtual std::optional<Slot> first_from(std::size_t bucket) const noexcept = 0;
    [[nodiscard]] virtual std::optional<Slot> next(Slot slot) const noexcept = 0;
    [[nodiscard]] virtual const std::string& key_at(Slot slot) const noexcept = 0;
    [[nodiscard]] virtual const Entry& entry_at(Slot slot) const noexcept = 0;

protected:
    IStorageEngine() = default;
    IStorageEngine(const IStorageEngine&) = default;
    IStorageEngine& operator=(const IStorageEngine&) = default;
};

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

// Какой движок.
enum class EngineKind {
    Std,       // std::unordered_map, стандартный хеш
    Hash,      // собственная таблица, цепочки, FNV-1a со случайной затравкой
    BadHash,   // та же таблица с суммой кодов символов — для контраста
    Open,      // звёздочка: открытая адресация, линейное пробирование, FNV-1a
};

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

inline constexpr std::array<EngineKind, 4> all_engine_kinds = {EngineKind::Std, EngineKind::Hash,
                                                               EngineKind::BadHash, EngineKind::Open};

[[nodiscard]] inline std::optional<EngineKind> parse_engine_kind(std::string_view name) noexcept {
    // Поиск по проекции: find сравнивает с name не сам элемент, а name_of
    // от него.
    const auto found = std::ranges::find(all_engine_kinds, name, name_of);
    return found == all_engine_kinds.end() ? std::nullopt : std::optional<EngineKind>(*found);
}

// Движок по умолчанию — из переменной окружения KVS_ENGINE, иначе std. Так
// весь набор тестов прогоняется на каждом движке без единой правки: ctest
// запускает его четыре раза с разным KVS_ENGINE (CMakeLists.txt). Неизвестное
// имя — std: тесты не должны падать из-за опечатки в окружении, а консольный
// клиент проверяет --engine сам и отказывает с ошибкой.
[[nodiscard]] inline EngineKind default_engine_kind() noexcept {
    static const EngineKind kind = [] {
        const char* const name = std::getenv("KVS_ENGINE");
        return name == nullptr ? EngineKind::Std : parse_engine_kind(name).value_or(EngineKind::Std);
    }();
    return kind;
}

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
