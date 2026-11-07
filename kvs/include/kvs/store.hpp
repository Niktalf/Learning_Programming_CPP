#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <generator>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kvs/concepts.hpp"
#include "kvs/errors.hpp"
#include "kvs/glob.hpp"
#include "kvs/value.hpp"
#include "kvs/version.hpp"

namespace kvs {

// Хранилище параметризовано типом значения по одной практической причине:
// в тестах вместо Value подставляется инструментированный тип, считающий
// копирования и перемещения. Без этого утверждение «вставка временного
// объекта не копирует» проверить нечем.
//
// Этап 5: требования к типу значения записаны концепцией StorableValue.
// Подставить тип без size() или с бросающим перемещением — одна строка
// диагностики, а не ошибка где-то в глубине std::map.
template <StorableValue V>
class BasicStore {
public:
    BasicStore() = default;

    // Этап 3: хранилище с ограничением объёма, как maxmemory у Redis. Объём
    // считается приближённо — длина ключа плюс размер значения, — и этого
    // достаточно, чтобы отказать записи до того, как что-либо изменено.
    explicit BasicStore(std::size_t memory_limit) noexcept : memory_limit_(memory_limit) {}

    // Сильная гарантия: всё, что может бросить, — проверка предела и вставка —
    // происходит до изменения учёта занятой памяти. Бросок оставляет
    // хранилище прежним.
    //
    // Этап 5: каждая запись получает версию — следующее поколение ключа и
    // отметку логических часов хранилища. Версия лежит в той же записи
    // таблицы, что и значение: одна вставка вместо двух, и согласовывать
    // два контейнера при броске не нужно.
    void set(std::string key, V value, std::string author = "local") {
        const std::size_t added = footprint(key, value);
        const auto it = data_.find(key);
        const std::size_t removed = it == data_.end() ? 0 : footprint(it->first, it->second.value);
        const std::size_t next_used = used_ - removed + added;
        require_room(key, next_used);

        const std::uint64_t generation = it == data_.end() ? 1 : it->second.version.generation + 1;
        Entry entry{std::move(value), RecordVersion{generation, clock_ + 1, std::move(author)}};
        // insert_or_assign из C++17: одна операция вместо поиска и вставки.
        // Бросить может только выделение узла для нового ключа, и тогда
        // ничего не изменено; дальше — операции, которые не бросают.
        data_.insert_or_assign(std::move(key), std::move(entry));
        used_ = next_used;
        ++clock_;
    }

    // Этап 6: GETSET — прочитать старое значение и записать новое одной
    // операцией хранилища. На этапе 11 она пройдёт под одним захватом
    // мьютекса; пара get + set была бы гонкой. Сильная гарантия: копия
    // старого значения снимается до записи, и если запись бросит, хранилище
    // прежнее.
    std::optional<V> exchange(std::string key, V value, std::string author = "local") {
        std::optional<V> old = get(key);
        set(std::move(key), std::move(value), std::move(author));
        return old;
    }

    // Этап 2: вместо сырого указателя — std::optional. Отсутствие значения
    // теперь выражено в типе, и вызывающий не может забыть о проверке:
    // разыменование пустого optional не проходит молча, как обращение
    // по nullptr.
    //
    // Возвращается копия, а не ссылка: std::optional<T&> стандартом не
    // предусмотрен. Для небольшого значения копия приемлема; там, где она
    // была бы дорогой, пришлось бы брать reference_wrapper или указатель,
    // и это стоит помнить как ограничение, а не считать optional бесплатным.
    //
    // Этап 5: ключ — любой KeyLike: std::string, std::string_view или
    // литерал. get(42) не соберётся с внятным сообщением.
    template <KeyLike K>
    [[nodiscard]] std::optional<V> get(const K& key) const {
        const auto it = data_.find(key_view(key));
        if (it == data_.end()) {
            return std::nullopt;
        }
        return it->second.value;
    }

    template <KeyLike K>
    [[nodiscard]] bool contains(const K& key) const noexcept {
        return data_.find(key_view(key)) != data_.end();
    }

    template <KeyLike K>
    [[nodiscard]] std::optional<RecordVersion> version(const K& key) const {
        const auto it = data_.find(key_view(key));
        if (it == data_.end()) {
            return std::nullopt;
        }
        return it->second.version;
    }

    // --- Этап 5: KEYS через ranges --------------------------------------
    //
    // Ленивый конвейер: views::keys над таблицей, filter по предикату,
    // take по пределу. Ничего не материализуется до take: предикат
    // вызывается, только пока не набралось limit подходящих ключей. Тест
    // Keys.PipelineIsLazy проверяет это счётчиком вызовов.
    //
    // Порядок filter и take существен. take | filter взял бы первые limit
    // ключей подряд и отфильтровал бы среди них — результат другой и почти
    // наверняка неверный.
    //
    // Возвращается вектор, а не само представление. Представление не владеет
    // и смотрит в таблицу: любая запись, пока его обходят, сделала бы его
    // висячим — тот же урок, что со string_view на этапе 2.
    template <std::predicate<std::string_view> Predicate>
    [[nodiscard]] std::vector<std::string> keys_where(Predicate predicate, std::size_t limit) const {
        // Предел обрезается по размеру таблицы до того, как попасть в take:
        // take принимает знаковую разность итераторов, и size_t больше
        // PTRDIFF_MAX стал бы отрицательным числом — нарушение предусловия
        // counted_iterator. KEYS * 18446744073709551615 иначе ронял бы
        // отладочную сборку, а в обычной был бы неопределённым поведением.
        const auto bounded = static_cast<std::ptrdiff_t>(std::min(limit, data_.size()));
        auto selected = data_ | std::views::keys |
                        std::views::filter([&predicate](const std::string& key) {
                            return std::invoke(predicate, std::string_view(key));
                        }) |
                        std::views::take(bounded);
        // Материализация — обычным циклом, а не std::ranges::to. ranges::to
        // для прямого (forward) диапазона сначала проходит его целиком, чтобы
        // узнать размер и зарезервировать память, и лишь затем копирует: в
        // libstdc++ предикат вызывается почти вдвое чаще. Для дешёвого
        // предиката это неважно, для glob_match по тысячам ключей — заметно.
        std::vector<std::string> result;
        result.reserve(limit < data_.size() ? limit : data_.size());
        for (const std::string& key : selected) {
            result.push_back(key);
        }
        return result;
    }

    [[nodiscard]] std::vector<std::string> keys(std::string_view pattern, std::size_t limit) const {
        return keys_where([pattern](std::string_view key) { return glob_match(pattern, key); },
                          limit);
    }

    // Звёздочка этапа 5: те же ключи генератором. Корутина отдаёт ключи по
    // одному и приостанавливается между ними. Если обход прерван — break,
    // исключение, — кадр корутины уничтожается в точке приостановки, и её
    // локальные объекты разрушаются как положено: RAII работает и внутри
    // корутины. Плата — кадр корутины в куче, и устранить это выделение
    // компилятор может далеко не всегда.
    //
    // Генератор, как и представление, смотрит в таблицу: обходить его можно
    // только пока хранилище не меняется.
    [[nodiscard]] std::generator<const std::string&> keys_lazily(std::string pattern) const {
        for (const auto& [key, entry] : data_) {
            if (glob_match(pattern, key)) {
                co_yield key;
            }
        }
    }

    // Этап 3: несколько ключей одной операцией с сильной гарантией. Либо
    // записаны все пары, либо хранилище не изменилось вовсе.
    //
    // Схема — copy-and-swap. Всё, что может бросить, делается над копией:
    // копирование таблицы, вставка каждой пары. Исходные данные при этом не
    // затронуты, и бросок на любом шаге оставляет их целыми. В конце копия
    // меняется местами с оригиналом операцией, которая бросить не может.
    // Это точка невозврата: до неё отказ ничего не портит, после неё отказа
    // не бывает.
    //
    // Цена сознательная: полная копия таблицы на каждую операцию. Для
    // настоящего хранилища это неприемлемо, и на этапе 11 с шардами вопрос
    // вернётся. Альтернатива — журнал отката, но она сложнее в разы, и откат
    // сам может бросить.
    void mset(const std::vector<std::pair<std::string, V>>& pairs) {
        BasicStore next = *this;
        for (const auto& [key, value] : pairs) {
            next.set(key, value);   // может бросить: копия, предел объёма
        }
        swap(*this, next);   // noexcept: точка невозврата
    }

    // swap помечен noexcept, и это не косметика. mset строит новое состояние
    // в стороне и последней операцией меняет его местами с текущим. Если бы
    // обмен мог бросить, он перестал бы быть точкой невозврата: бросок на
    // середине обмена оставил бы таблицу от одного состояния, а счётчик
    // занятой памяти — от другого, и сильная гарантия молча выродилась бы
    // в базовую. Обмен std::map и целых не бросает никогда — это и проверяет
    // тест Guarantee.SwapIsNoexcept.
    friend void swap(BasicStore& a, BasicStore& b) noexcept {
        a.data_.swap(b.data_);
        std::swap(a.used_, b.used_);
        std::swap(a.memory_limit_, b.memory_limit_);
        std::swap(a.clock_, b.clock_);
    }

    template <KeyLike K>
    bool del(const K& key) {
        const auto it = data_.find(key_view(key));
        if (it == data_.end()) {
            return false;
        }
        used_ -= footprint(it->first, it->second.value);
        data_.erase(it);
        return true;
    }

    [[nodiscard]] std::size_t used_bytes() const noexcept { return used_; }
    [[nodiscard]] std::optional<std::size_t> memory_limit() const noexcept {
        return memory_limit_;
    }

    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }

    // Функтор принимается по значению — как это делают алгоритмы стандартной
    // библиотеки. Именно поэтому лямбда с захватом по значению и mutable не
    // выносит изменения наружу: меняется копия.
    //
    // Structured bindings из C++17: пара раскладывается на имена, а не
    // читается через first и second.
    template <class F>
    void for_each(F fn) const {
        for (const auto& [key, entry] : data_) {
            fn(key, entry.value);
        }
    }

private:
    // std::less<> включает разнородный поиск: методы принимают string_view
    // и не создают временную строку на каждый вызов.
    struct Entry {
        V value;
        RecordVersion version;
    };
    using Map = std::map<std::string, Entry, std::less<>>;

    [[nodiscard]] static std::size_t footprint(const std::string& key, const V& value) noexcept {
        return key.size() + value.size();
    }

    void require_room(const std::string& key, std::size_t needed) const {
        if (memory_limit_ && needed > *memory_limit_) {
            throw OutOfMemoryError(key, needed, *memory_limit_);
        }
    }

    Map data_;
    std::size_t used_ = 0;
    std::optional<std::size_t> memory_limit_;
    std::uint64_t clock_ = 0;   // логические часы: +1 на каждую запись
};

using Store = BasicStore<Value>;

}  // namespace kvs
