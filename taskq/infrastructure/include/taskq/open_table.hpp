#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace taskq {

// Звёздочка этапа 8: открытая адресация с линейным пробированием.
//
// Все элементы лежат в одном векторе ячеек. Ключ с хешем h ищется в ячейке
// h & mask, и если она занята другим ключом — в следующей, и так до пустой.
// Цепочек нет: «цепочка» — это пробег по соседним ячейкам, подряд в памяти.
//
// Удаление — главное для индекса задач: задачи постоянно приходят и уходят,
// и каждая выдача (POLL) удаляет запись. Освободить ячейку — значит сделать
// её пустой, а пустая ячейка для поиска означает «дальше искать нечего».
// Если задача 17 при постановке пропробировала мимо занятой задачей 5 и
// легла дальше, то после выдачи задачи 5 с пустой ячейкой поиск задачи 17
// остановится на её месте и скажет «нет такой»: ACK 17 и STATUS 17 ответили
// бы unknown task, хотя задача ждёт в очереди. Поэтому удалённая ячейка
// становится надгробием (tombstone): для поиска — занята («иди дальше»), для
// вставки — свободна («можно положить сюда»). Плата — надгробия удлиняют
// пробеги, и порог заполнения считается по живым вместе с надгробиями:
// иначе индекс, через который прошли миллионы задач при десятке живых,
// зарос бы надгробиями без единого рехеширования, и поиск отсутствующего
// номера обходил бы его целиком.
//
// В отличие от метода цепочек, при рехешировании элементы переезжают, и
// ссылки на значения повисают. Хранилище держит указатель на запись
// индекса только внутри одной операции, без вставок между поиском и
// использованием, — для него это допустимо.
template <class K, class V, class Hash, class Equal = std::equal_to<>>
class OpenTable {
    struct Cell {
        std::optional<std::pair<K, V>> item;   // пусто и не надгробие — ячейка свободна
        bool tombstone = false;
    };

public:
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

    // Вставка ключа, которого в таблице нет. Рехеширование — до вставки:
    // бросить может только выделение нового вектора, до первого изменения.
    // emplace в optional перемещает ключ и значение, а перемещения не
    // бросают (это требование к K и V хранилища).
    V& insert_new(K key, V value) {
        if (static_cast<double>(used_ + 1) > max_load_ * static_cast<double>(cells_.size())) {
            // Если место съели надгробия, а живых мало, хватит перестроить
            // таблицу того же размера — рехеширование их и убирает.
            const bool mostly_tombstones =
                static_cast<double>(size_ + 1) <= max_load_ * static_cast<double>(cells_.size()) / 2;
            rehash(mostly_tombstones ? cells_.size() : cells_.size() * 2);
        }
        Cell& cell = cells_[free_cell(key)];
        cell.item.emplace(std::move(key), std::move(value));
        if (!cell.tombstone) {
            ++used_;   // занята бывшая пустая; надгробие уже было учтено
        }
        cell.tombstone = false;
        ++size_;
        return cell.item->second;
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

    // Перестройка в таблицу на cells ячеек: живые элементы переносятся
    // перемещением, надгробия пропадают.
    void rehash(std::size_t cells) {
        OpenTable next(hash_, max_load_, cells);
        for (Cell& cell : cells_) {
            if (cell.item) {
                next.cells_[next.free_cell(cell.item->first)].item = std::move(cell.item);
                ++next.size_;
            }
        }
        cells_.swap(next.cells_);
        used_ = size_;
    }

    void reserve(std::size_t count) {
        const auto needed = static_cast<std::size_t>(static_cast<double>(count) / max_load_) + 1;
        if (needed > cells_.size()) {
            rehash(needed);
        }
    }

    template <class F>
    void for_each(F fn) {
        for (Cell& cell : cells_) {
            if (cell.item) {
                fn(std::as_const(cell.item->first), cell.item->second);
            }
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t bucket_count() const noexcept { return cells_.size(); }
    [[nodiscard]] double load_factor() const noexcept {
        return static_cast<double>(size_) / static_cast<double>(cells_.size());
    }
    [[nodiscard]] std::size_t tombstones() const noexcept { return used_ - size_; }

    // Длина пробега: сколько ячеек посмотрит поиск ключа — до самого ключа
    // или, для отсутствующего, до первой пустой. Для сравнения с длиной
    // цепочки и с оценкой Кнута (docs/hashing.md).
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

}  // namespace taskq
