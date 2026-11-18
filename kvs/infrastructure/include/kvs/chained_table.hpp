#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace kvs {

// Этап 8: собственная хеш-таблица, метод цепочек.
//
// Устройство: вектор корзин, в каждой — односвязный список узлов. Узел
// владеет следующим через unique_ptr, корзина владеет первым. Число корзин
// — степень двойки, и номер корзины — младшие биты хеша: hash & (n - 1),
// маска вместо остатка от деления: деление 64-битных чисел стоит десятки
// тактов, маска — один.
//
// Коэффициент заполнения — size / bucket_count. Когда очередная вставка
// поднимет его выше порога (по умолчанию 1.0), таблица удваивается:
// рехеширование. Узлы при этом не копируются и не перемещаются — только
// перевешиваются в новые корзины. Поэтому указатели и ссылки на значения
// переживают рехеширование, а позиции обхода — нет.
//
// Почему список, а не вектор в каждой корзине. Вектор держал бы цепочку
// подряд в памяти, но при рехешировании переносил бы значения, и ссылка на
// значение, взятая до вставки, повисла бы. Список — классический метод
// цепочек, и он даёт ту же стабильность ссылок, что std::unordered_map.
template <class K, class V, class Hash, class Equal = std::equal_to<>>
class ChainedTable {
    struct Node {
        K key;
        V value;
        std::unique_ptr<Node> next;
    };

public:
    // Позиция обхода: корзина и номер узла в цепочке.
    struct Slot {
        std::size_t bucket = 0;
        std::size_t index = 0;
        friend bool operator==(const Slot&, const Slot&) = default;
    };

    explicit ChainedTable(Hash hash = {}, double max_load = 1.0, std::size_t buckets = 8)
        : buckets_(round_up(buckets)), hash_(std::move(hash)), max_load_(max_load) {}

    // Копия — глубокая и с тем же числом корзин, порядок цепочек сохраняется.
    // Строится в стороне и только затем становится объектом: бросок
    // копирования значения оставляет исходную таблицу нетронутой.
    ChainedTable(const ChainedTable& other)
        : buckets_(other.buckets_.size()), hash_(other.hash_), equal_(other.equal_),
          max_load_(other.max_load_) {
        for (std::size_t b = 0; b < other.buckets_.size(); ++b) {
            std::unique_ptr<Node>* tail = &buckets_[b];
            for (const Node* node = other.buckets_[b].get(); node != nullptr; node = node->next.get()) {
                *tail = std::make_unique<Node>(Node{node->key, node->value, nullptr});
                tail = &(*tail)->next;
                ++size_;
            }
        }
    }

    ChainedTable& operator=(const ChainedTable& other) {
        ChainedTable copy(other);
        swap(copy);
        return *this;
    }

    ChainedTable(ChainedTable&&) noexcept = default;
    ChainedTable& operator=(ChainedTable&&) noexcept = default;

    // Деструктор — цикл, а не рекурсия. Узел владеет следующим через
    // unique_ptr, и по умолчанию цепочка разрушалась бы рекурсивно: каждый
    // ~Node вызывает ~unique_ptr следующего. С плохой хеш-функцией цепочка —
    // десятки тысяч узлов, и рекурсия такой глубины переполняет стек.
    ~ChainedTable() { clear(); }

    void swap(ChainedTable& other) noexcept {
        buckets_.swap(other.buckets_);
        std::swap(size_, other.size_);
        std::swap(hash_, other.hash_);
        std::swap(equal_, other.equal_);
        std::swap(max_load_, other.max_load_);
    }

    template <class Key>
    [[nodiscard]] V* find(const Key& key) noexcept {
        Node* const node = find_node(key);
        return node == nullptr ? nullptr : &node->value;
    }

    template <class Key>
    [[nodiscard]] const V* find(const Key& key) const noexcept {
        const Node* const node = const_cast<ChainedTable*>(this)->find_node(key);
        return node == nullptr ? nullptr : &node->value;
    }

    // Вставка или замена. Сильная гарантия: рехеширование — до вставки, и
    // бросить в нём может только выделение нового массива корзин, а до него
    // ничего не изменено; узел выделяется до того, как его прицепят.
    // Возвращает true, если ключ новый.
    bool insert_or_assign(K key, V value) {
        if (Node* const existing = find_node(key)) {
            existing->value = std::move(value);
            return false;
        }
        if (static_cast<double>(size_ + 1) > max_load_ * static_cast<double>(buckets_.size())) {
            rehash(buckets_.size() * 2);
        }
        std::unique_ptr<Node>& head = buckets_[bucket_of(key)];
        head = std::make_unique<Node>(Node{std::move(key), std::move(value), std::move(head)});
        ++size_;
        return true;
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

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t bucket_count() const noexcept { return buckets_.size(); }
    [[nodiscard]] double load_factor() const noexcept {
        return static_cast<double>(size_) / static_cast<double>(buckets_.size());
    }
    [[nodiscard]] double max_load_factor() const noexcept { return max_load_; }

    // Длина цепочки — для гистограммы распределения (docs/hashing.md).
    [[nodiscard]] std::size_t chain_length(std::size_t bucket) const noexcept {
        std::size_t length = 0;
        for (const Node* node = buckets_[bucket].get(); node != nullptr; node = node->next.get()) {
            ++length;
        }
        return length;
    }

    // --- Обход по позициям -----------------------------------------------
    //
    // Первая занятая позиция в корзине bucket или дальше; пусто — дальше
    // ничего нет.
    [[nodiscard]] std::optional<Slot> first_from(std::size_t bucket) const noexcept {
        for (std::size_t b = bucket; b < buckets_.size(); ++b) {
            if (buckets_[b] != nullptr) {
                return Slot{b, 0};
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Slot> next(Slot slot) const noexcept {
        if (node_at(Slot{slot.bucket, slot.index})->next != nullptr) {
            return Slot{slot.bucket, slot.index + 1};
        }
        return first_from(slot.bucket + 1);
    }

    // Доступ по позиции — проход по цепочке до номера index. Цепочки
    // короткие, если хеш хорош; с плохим хешем медленнеет и обход — это
    // тоже видно в docs/hashing.md.
    [[nodiscard]] const K& key_at(Slot slot) const noexcept { return node_at(slot)->key; }
    [[nodiscard]] const V& value_at(Slot slot) const noexcept { return node_at(slot)->value; }

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

    [[nodiscard]] const Node* node_at(Slot slot) const noexcept {
        const Node* node = buckets_[slot.bucket].get();
        for (std::size_t i = 0; i < slot.index; ++i) {
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

}  // namespace kvs
