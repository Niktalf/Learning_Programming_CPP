#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace taskq {

// Этап 8: собственная хеш-таблица, метод цепочек. В очереди задач она держит
// индекс по номеру задачи — его ищут ACK, FAIL, STATUS, DELAY и BUMP.
//
// Устройство: вектор корзин, в каждой — односвязный список узлов. Узел
// владеет следующим через unique_ptr, корзина владеет первым. Число корзин
// — степень двойки, и номер корзины — младшие биты хеша: hash & (n - 1),
// маска вместо остатка от деления. Деление 64-битных чисел — десятки
// тактов, маска — один; плата — хеш обязан хорошо перемешивать младшие
// биты, и FNV-1a это делает (docs/hashing.md).
//
// Коэффициент заполнения — size / bucket_count. Когда очередная вставка
// подняла бы его выше порога (по умолчанию 1.0), таблица удваивается:
// рехеширование. Узлы при этом не копируются и не перемещаются — только
// перевешиваются в новые корзины. Поэтому ссылки на значения переживают
// рехеширование, как и в std::unordered_map.
//
// Таблица только перемещаемая: копия индекса, хранящего итераторы на узлы
// деревьев очередей, смотрела бы в чужие деревья и смысла не имеет.
template <class K, class V, class Hash, class Equal = std::equal_to<>>
class ChainedTable {
    struct Node {
        K key;
        V value;
        std::unique_ptr<Node> next;
    };

public:
    explicit ChainedTable(Hash hash = {}, double max_load = 1.0, std::size_t buckets = 8)
        : buckets_(round_up(buckets)), hash_(std::move(hash)), max_load_(max_load) {}

    ChainedTable(const ChainedTable&) = delete;
    ChainedTable& operator=(const ChainedTable&) = delete;
    ChainedTable(ChainedTable&&) noexcept = default;
    ChainedTable& operator=(ChainedTable&&) noexcept = default;

    // Деструктор — цикл, а не рекурсия. По умолчанию цепочка разрушалась бы
    // рекурсивно: каждый ~Node вызывает ~unique_ptr следующего. С плохой
    // хеш-функцией цепочка — десятки тысяч узлов, и рекурсия такой глубины
    // переполняет стек.
    ~ChainedTable() { clear(); }

    template <class Key>
    [[nodiscard]] V* find(const Key& key) noexcept {
        Node* const node = find_node(key);
        return node == nullptr ? nullptr : &node->value;
    }

    template <class Key>
    [[nodiscard]] const V* find(const Key& key) const noexcept {
        return const_cast<ChainedTable*>(this)->find(key);
    }

    // Вставка ключа, которого в таблице нет (предусловие — проверяет
    // вызывающий). Сильная гарантия: рехеширование — до вставки, и бросить в
    // нём может только выделение нового массива корзин, до первого
    // изменения; узел выделяется до того, как его прицепят. Возвращает
    // ссылку на вставленное значение.
    V& insert_new(K key, V value) {
        if (static_cast<double>(size_ + 1) > max_load_ * static_cast<double>(buckets_.size())) {
            rehash(buckets_.size() * 2);
        }
        std::unique_ptr<Node>& head = buckets_[bucket_of(key)];
        auto node = std::make_unique<Node>(Node{std::move(key), std::move(value), nullptr});
        node->next = std::move(head);
        head = std::move(node);
        ++size_;
        return head->value;
    }

    // Удаление не бросает: поиск, перецепка указателя, разрушение узла.
    template <class Key>
    bool erase(const Key& key) noexcept {
        std::unique_ptr<Node>* link = &buckets_[bucket_of(key)];
        while (*link != nullptr && !equal_((*link)->key, key)) {
            link = &(*link)->next;
        }
        if (*link == nullptr) {
            return false;
        }
        *link = std::move((*link)->next);
        --size_;
        return true;
    }

    void clear() noexcept {
        for (std::unique_ptr<Node>& head : buckets_) {
            while (head != nullptr) {
                head = std::move(head->next);   // старая голова разрушается без рекурсии
            }
        }
        size_ = 0;
    }

    // Перевешивание узлов в новый массив корзин. Бросить может только
    // выделение массива — до первого изменения.
    void rehash(std::size_t buckets) {
        std::vector<std::unique_ptr<Node>> next(round_up(std::max(buckets, std::size_t{1})));
        const std::size_t mask = next.size() - 1;
        for (std::unique_ptr<Node>& head : buckets_) {
            while (head != nullptr) {
                std::unique_ptr<Node> node = std::move(head);
                head = std::move(node->next);
                std::unique_ptr<Node>& target = next[hash_(node->key) & mask];
                node->next = std::move(target);
                target = std::move(node);
            }
        }
        buckets_.swap(next);
    }

    // Заранее — столько корзин, чтобы count элементов поместились без
    // рехеширования. Нужна пакетной постановке: рост — до первой вставки.
    void reserve(std::size_t count) {
        const auto needed = static_cast<std::size_t>(static_cast<double>(count) / max_load_) + 1;
        if (needed > buckets_.size()) {
            rehash(needed);
        }
    }

    // Обход всех пар в порядке корзин — для замеров. Порядок не определён и меняется при рехешировании.
    template <class F>
    void for_each(F fn) {
        for (const std::unique_ptr<Node>& head : buckets_) {
            for (Node* node = head.get(); node != nullptr; node = node->next.get()) {
                fn(std::as_const(node->key), node->value);
            }
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t bucket_count() const noexcept { return buckets_.size(); }
    [[nodiscard]] double load_factor() const noexcept {
        return static_cast<double>(size_) / static_cast<double>(buckets_.size());
    }

    // Длина цепочки — для гистограммы распределения (docs/hashing.md).
    [[nodiscard]] std::size_t chain_length(std::size_t bucket) const noexcept {
        std::size_t length = 0;
        for (const Node* node = buckets_[bucket].get(); node != nullptr; node = node->next.get()) {
            ++length;
        }
        return length;
    }

private:
    [[nodiscard]] static std::size_t round_up(std::size_t n) noexcept {
        std::size_t power = 1;
        while (power < n) {
            power *= 2;
        }
        return power;
    }

    template <class Key>
    [[nodiscard]] std::size_t bucket_of(const Key& key) const noexcept {
        return hash_(key) & (buckets_.size() - 1);
    }

    template <class Key>
    [[nodiscard]] Node* find_node(const Key& key) noexcept {
        Node* node = buckets_[bucket_of(key)].get();
        while (node != nullptr && !equal_(node->key, key)) {
            node = node->next.get();
        }
        return node;
    }

    std::vector<std::unique_ptr<Node>> buckets_;
    std::size_t size_ = 0;
    Hash hash_;
    Equal equal_;
    double max_load_;
};

}  // namespace taskq
