#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace kvs {

// Звёздочка этапа 8: открытая адресация с линейным пробированием.
//
// Все элементы лежат в одном векторе ячеек. Ключ с хешем h ищется в ячейке
// h & mask, и если она занята другим ключом — в следующей, и так до пустой.
// Цепочек нет: «цепочка» — это пробег по соседним ячейкам, подряд в памяти,
// и в этом весь выигрыш — один поток чтения вместо прыжков по узлам.
//
// Удаление. Освободить ячейку — значит сделать её пустой, а пустая ячейка
// для поиска — «дальше искать нечего». Если ключ B при вставке пропробировал
// мимо занятой A и лёг дальше, то после удаления A с пустой ячейкой поиск B
// остановится на месте A и скажет «нет такого», хотя B в таблице. Поэтому
// удалённая ячейка становится надгробием (tombstone): для поиска — занята
// («иди дальше»), для вставки — свободна («можно положить сюда»). Плата —
// надгробия занимают место и удлиняют пробеги; их убирает рехеширование.
// Порог заполнения считается по занятым ячейкам вместе с надгробиями: иначе
// таблица, где ключи постоянно приходят и уходят, заросла бы надгробиями
// без единого рехеширования, и поиск отсутствующего ключа обходил бы её
// целиком.
template <class K, class V, class Hash, class Equal = std::equal_to<>>
class OpenTable {
    struct Cell {
        std::optional<std::pair<K, V>> item;   // пусто и не надгробие — ячейка свободна
        bool tombstone = false;
    };

public:
    struct Slot {
        std::size_t bucket = 0;
        std::size_t index = 0;   // всегда 0: в ячейке один элемент
        friend bool operator==(const Slot&, const Slot&) = default;
    };

    explicit OpenTable(Hash hash = {}, double max_load = 0.75, std::size_t cells = 8)
        : cells_(round_up(cells)), hash_(std::move(hash)), max_load_(max_load) {}

    template <class Key>
    [[nodiscard]] V* find(const Key& key) noexcept {
        const std::optional<std::size_t> at = find_cell(key);
        return at ? &cells_[*at].item->second : nullptr;
    }

    template <class Key>
    [[nodiscard]] const V* find(const Key& key) const noexcept {
        const std::optional<std::size_t> at = find_cell(key);
        return at ? &cells_[*at].item->second : nullptr;
    }

    // Вставка или замена. Рехеширование — до вставки; при нём элементы
    // переносятся перемещением, и ссылки на значения повисают — в отличие
    // от метода цепочек.
    bool insert_or_assign(K key, V value) {
        if (const std::optional<std::size_t> at = find_cell(key)) {
            cells_[*at].item->second = std::move(value);
            return false;
        }
        if (static_cast<double>(used_ + 1) > max_load_ * static_cast<double>(cells_.size())) {
            // Если место съели надгробия, а живых мало, хватит перестроить
            // таблицу того же размера — рехеширование их и убирает.
            const bool mostly_tombstones =
                static_cast<double>(size_ + 1) <= max_load_ * static_cast<double>(cells_.size()) / 2;
            rehash(mostly_tombstones ? cells_.size() : cells_.size() * 2);
        }
        const std::size_t at = free_cell(key);
        Cell& cell = cells_[at];
        cell.item.emplace(std::move(key), std::move(value));
        if (!cell.tombstone) {
            ++used_;   // занята бывшая пустая; надгробие уже было учтено
        }
        cell.tombstone = false;
        ++size_;
        return true;
    }

    template <class Key>
    bool erase(const Key& key) noexcept {
        const std::optional<std::size_t> at = find_cell(key);
        if (!at) {
            return false;
        }
        cells_[*at].item.reset();
        cells_[*at].tombstone = true;   // не пустая: пробеги через неё должны продолжаться
        --size_;
        return true;
    }

    void clear() noexcept {
        for (Cell& cell : cells_) {
            cell.item.reset();
            cell.tombstone = false;
        }
        size_ = 0;
        used_ = 0;
    }

    // Перестройка в таблицу на cells ячеек: живые элементы переносятся
    // перемещением, надгробия пропадают. Бросить может только выделение
    // нового вектора — до первого изменения.
    void rehash(std::size_t cells) {
        OpenTable next(hash_, max_load_, cells);
        for (Cell& cell : cells_) {
            if (cell.item) {
                const std::size_t at = next.free_cell(cell.item->first);
                next.cells_[at].item = std::move(cell.item);
                ++next.used_;
                ++next.size_;
            }
        }
        cells_.swap(next.cells_);
        used_ = size_;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t bucket_count() const noexcept { return cells_.size(); }
    [[nodiscard]] double load_factor() const noexcept {
        return static_cast<double>(size_) / static_cast<double>(cells_.size());
    }
    [[nodiscard]] std::size_t tombstones() const noexcept { return used_ - size_; }

    // Длина пробега для ключа, который лежит в ячейке: сколько ячеек
    // посмотрит его поиск. Для сравнения с длиной цепочки (docs/hashing.md).
    template <class Key>
    [[nodiscard]] std::size_t probe_length(const Key& key) const noexcept {
        std::size_t length = 1;
        for (std::size_t at = hash_(key) & mask();; at = (at + 1) & mask(), ++length) {
            const Cell& cell = cells_[at];
            if ((cell.item && equal_(cell.item->first, key)) || (!cell.item && !cell.tombstone)) {
                return length;
            }
        }
    }

    [[nodiscard]] std::optional<Slot> first_from(std::size_t bucket) const noexcept {
        for (std::size_t b = bucket; b < cells_.size(); ++b) {
            if (cells_[b].item) {
                return Slot{b, 0};
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Slot> next(Slot slot) const noexcept { return first_from(slot.bucket + 1); }
    [[nodiscard]] const K& key_at(Slot slot) const noexcept { return cells_[slot.bucket].item->first; }
    [[nodiscard]] const V& value_at(Slot slot) const noexcept { return cells_[slot.bucket].item->second; }

private:
    [[nodiscard]] static std::size_t round_up(std::size_t n) noexcept {
        std::size_t power = 1;
        while (power < n) {
            power *= 2;
        }
        return power;
    }

    [[nodiscard]] std::size_t mask() const noexcept { return cells_.size() - 1; }

    // Поиск: от домашней ячейки до первой пустой, мимо надгробий. Пустая
    // ячейка в таблице есть всегда — порог заполнения меньше единицы.
    template <class Key>
    [[nodiscard]] std::optional<std::size_t> find_cell(const Key& key) const noexcept {
        for (std::size_t at = hash_(key) & mask();; at = (at + 1) & mask()) {
            const Cell& cell = cells_[at];
            if (cell.item) {
                if (equal_(cell.item->first, key)) {
                    return at;
                }
            } else if (!cell.tombstone) {
                return std::nullopt;
            }
        }
    }

    // Место для нового ключа: первое надгробие или первая пустая ячейка.
    // Вызывается, только когда ключа в таблице точно нет.
    template <class Key>
    [[nodiscard]] std::size_t free_cell(const Key& key) const noexcept {
        for (std::size_t at = hash_(key) & mask();; at = (at + 1) & mask()) {
            if (!cells_[at].item) {
                return at;
            }
        }
    }

    std::vector<Cell> cells_;
    std::size_t size_ = 0;   // живые элементы
    std::size_t used_ = 0;   // живые и надгробия: то, что удлиняет пробеги
    Hash hash_;
    Equal equal_;
    double max_load_;
};

}  // namespace kvs
