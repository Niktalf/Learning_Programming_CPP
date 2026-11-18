#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <generator>
#include <iterator>
#include <unordered_map>
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

// Этап 7: хеш для основной таблицы. Прозрачный (is_transparent): с ним и с
// std::equal_to<> поиск в unordered_map принимает string_view и не строит
// временную строку — то, что у std::map давал std::less<>. Разнородный
// поиск в неупорядоченных контейнерах появился только в C++20.
struct KeyHash {
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(std::string_view key) const noexcept {
        return std::hash<std::string_view>{}(key);
    }
};

// Хранилище параметризовано типом значения по одной практической причине:
// в тестах вместо Value подставляется инструментированный тип, считающий
// копирования и перемещения. Без этого утверждение «вставка временного
// объекта не копирует» проверить нечем.
//
// Этап 5: требования к типу значения записаны концепцией StorableValue.
// Подставить тип без size() или с бросающим перемещением — одна строка
// диагностики, а не ошибка где-то в глубине таблицы.
template <StorableValue V>
class BasicStore {
    // Типы таблицы — в начале класса: их используют вложенные итераторы,
    // а в объявлениях членов имя должно быть уже объявлено.
    struct Entry {
        V value;
        RecordVersion version;
    };

    // Этап 7: основная таблица — std::unordered_map, выбрана по замерам
    // (docs/benchmarks.md): на 10^6 ключей поиск в 11 раз быстрее std::map,
    // вставка — в 3,7 раза. Порядка ключей у неё нет; KEYS, которому он
    // нужен, сортирует свой результат сам, и это дешевле, чем платить за
    // порядок в каждом GET. Худший случай — ключи с одним хешем — разобран
    // там же; ответ на него — собственная хеш-функция этапа 8.
    //
    // KeyHash и std::equal_to<> включают разнородный поиск: методы
    // принимают string_view и не создают временную строку на каждый вызов.
    using Map = std::unordered_map<std::string, Entry, KeyHash, std::equal_to<>>;

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

    // Этап 7: изменение значения на месте — для команд списка. До этапа 7
    // RPUSH копировал список, дописывал элемент и записывал копию обратно:
    // O(n) на каждую вставку. Теперь список меняется там, где лежит.
    //
    // growth — на сколько байт значение может вырасти; предел объёма
    // проверяется по нему до изменения. fn получает V& и возвращает что
    // угодно; результат modify — результат fn. После fn учёт объёма
    // пересчитывается по фактическому размеру, и запись получает новую версию.
    //
    // Гарантия — та, что даёт fn: если fn при исключении оставляет значение
    // прежним (push_front и push_back у deque так и делают), хранилище тоже
    // прежнее. Ключа нет — KeyNotFoundError: создавать значение — дело set.
    template <KeyLike K, class F>
    auto modify(const K& key, std::size_t growth, F&& fn) {
        const auto it = data_.find(key_view(key));
        if (it == data_.end()) {
            throw KeyNotFoundError(std::string(key_view(key)));
        }
        require_room(it->first, used_ + growth);
        Entry& entry = it->second;
        const std::size_t before = entry.value.size();
        if constexpr (std::is_void_v<std::invoke_result_t<F, V&>>) {
            std::invoke(std::forward<F>(fn), entry.value);   // бросок — ничего не учтено
            account(entry, before);
        } else {
            auto result = std::invoke(std::forward<F>(fn), entry.value);
            account(entry, before);
            return result;
        }
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

    // Этап 7: доступ к значению без копии — для команд, которые только
    // читают. До этапа 7 GET, TYPE и LLEN шли через get и копировали
    // значение целиком, а для списка это копия всех его элементов ради
    // одного числа. Ссылка действительна, пока ключ не изменён и не удалён.
    template <KeyLike K>
    [[nodiscard]] std::optional<std::reference_wrapper<const V>> lookup(const K& key) const {
        const auto it = data_.find(key_view(key));
        if (it == data_.end()) {
            return std::nullopt;
        }
        return std::cref(it->second.value);
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
        //
        // Этап 7: конвейер идёт по самому хранилищу — его собственному
        // итератору, — а не по data_ | views::keys. Код конвейера этапа 5 не
        // изменился: ranges нужен только forward_iterator.
        const auto bounded = static_cast<std::ptrdiff_t>(std::min(limit, data_.size()));
        auto selected = *this |
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
        // Этап 7: у unordered_map порядка нет, и ключи пришли в порядке
        // корзин. Сортируется только результат — limit ключей, а не вся
        // таблица. При limit меньше числа подходящих ключей какие именно
        // попадут в ответ, не определено — как у KEYS и SCAN в Redis.
        std::ranges::sort(result);
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

    // --- Этап 7: собственный итератор по ключам -------------------------
    //
    // Прямой (forward) итератор: многопроходный, копируемый, сравнимый.
    // Обёртка над итератором таблицы, которая отдаёт только ключ: значение
    // и версия — подробности хранилища, снаружи их видно через get. Пять
    // типов-членов — то, что смотрят std::iterator_traits и концепции:
    // категория, тип значения, тип ссылки, указателя и разности.
    //
    // Итератор действителен, пока таблица не перестраивается: у
    // unordered_map вставка, вызвавшая рехеширование, делает недействительными
    // все итераторы (но не ссылки на элементы); удаление — только итератор
    // удалённого. Поэтому обходить хранилище можно, только пока в него не
    // пишут.
    class Iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::string;
        using difference_type = std::ptrdiff_t;
        using reference = const std::string&;
        using pointer = const std::string*;

        // Итератор по умолчанию требуется концепцией forward_iterator: он
        // сравним только с другим таким же и разыменовывать его нельзя.
        Iterator() = default;

        [[nodiscard]] reference operator*() const noexcept { return position_->first; }
        [[nodiscard]] pointer operator->() const noexcept { return &position_->first; }

        Iterator& operator++() noexcept {
            ++position_;
            return *this;
        }

        // Постфиксный инкремент возвращает копию до сдвига — это и отличает
        // forward-итератор от input: старую копию можно разыменовать.
        Iterator operator++(int) noexcept {
            Iterator before = *this;
            ++position_;
            return before;
        }

        // operator!= компилятор выводит из == сам (C++20).
        [[nodiscard]] friend bool operator==(const Iterator&, const Iterator&) = default;

    private:
        friend class BasicStore;
        explicit Iterator(typename Map::const_iterator position) noexcept : position_(position) {}

        typename Map::const_iterator position_{};
    };

    [[nodiscard]] Iterator begin() const noexcept { return Iterator(data_.begin()); }
    [[nodiscard]] Iterator end() const noexcept { return Iterator(data_.end()); }

    // --- Звёздочка этапа 7: ленивый просмотр со стражем ------------------
    //
    // scan(pattern) — диапазон ключей, подходящих под образец. Итератор —
    // input, а не forward: он однопроходный и некопируемый, а следующий
    // подходящий ключ ищет только при ++ — ничего не вычисляется заранее.
    // Конец — отдельный тип-страж ScanEnd, а не второй итератор: «конец»
    // здесь — не позиция, а условие «подходящих ключей больше нет», и
    // итератор проверяет его сам. Так устроены диапазоны C++20; парный
    // итератор конца пришлось бы хранить и сравнивать.
    struct ScanEnd {};

    class ScanIterator {
    public:
        using value_type = std::string;
        using difference_type = std::ptrdiff_t;

        ScanIterator(ScanIterator&&) noexcept = default;
        ScanIterator& operator=(ScanIterator&&) noexcept = default;

        [[nodiscard]] const std::string& operator*() const noexcept { return position_->first; }

        ScanIterator& operator++() {
            ++position_;
            skip();
            return *this;
        }

        // У input-итератора постфиксный инкремент может ничего не
        // возвращать: копии нет, разыменовывать старую позицию нечем.
        void operator++(int) { ++*this; }

        [[nodiscard]] friend bool operator==(const ScanIterator& it, ScanEnd) noexcept {
            return it.position_ == it.end_;
        }

    private:
        friend class BasicStore;
        ScanIterator(const Map& table, std::string_view pattern)
            : position_(table.begin()), end_(table.end()), pattern_(pattern) {
            skip();
        }

        // Сдвиг до ближайшего подходящего ключа — вся «ленивость» здесь.
        void skip() {
            while (position_ != end_ && !glob_match(pattern_, position_->first)) {
                ++position_;
            }
        }

        typename Map::const_iterator position_;
        typename Map::const_iterator end_;
        std::string_view pattern_;   // образец живёт у вызывающего, как и диапазон
    };

    // Диапазон смотрит в таблицу и в строку образца: оба обязаны пережить
    // обход.
    [[nodiscard]] auto scan(std::string_view pattern) const {
        return std::ranges::subrange<ScanIterator, ScanEnd>(ScanIterator(data_, pattern), ScanEnd{});
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
    // в базовую. Обмен таблиц и целых не бросает никогда — это и проверяет
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
    [[nodiscard]] static std::size_t footprint(const std::string& key, const V& value) noexcept {
        return key.size() + value.size();
    }

    // Учёт после изменения на месте: объём по фактическому размеру и новая
    // версия записи. Только арифметика и короткая строка автора.
    void account(Entry& entry, std::size_t before) {
        used_ = used_ - before + entry.value.size();
        entry.version = RecordVersion{entry.version.generation + 1, ++clock_, "local"};
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
