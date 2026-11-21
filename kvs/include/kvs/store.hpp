#pragma once

#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <generator>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kvs/clock.hpp"
#include "kvs/concepts.hpp"
#include "kvs/errors.hpp"
#include "kvs/glob.hpp"
#include "kvs/memory_budget.hpp"
#include "kvs/storage_engine.hpp"
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
// диагностики, а не ошибка где-то в глубине таблицы.
template <StorableValue V>
class BasicStore {
    // Типы таблицы — в начале класса: их используют вложенные итераторы,
    // а в объявлениях членов имя должно быть уже объявлено. Этап 9: запись —
    // открытый тип StoreEntry из storage_engine.hpp: движок создаётся вне
    // хранилища, и тому, кто его создаёт, нужно назвать тип записи.
    using Entry = StoreEntry<V>;

    // Этап 7: основная таблица — хеш-таблица, выбрана по замерам
    // (docs/benchmarks.md): на 10^6 ключей поиск в 11 раз быстрее std::map.
    // Этап 8: таблица — за интерфейсом движка (storage_engine.hpp), и какой именно
    // хеш-таблицей она будет — std::unordered_map, собственными цепочками
    // или открытой адресацией, — решается при создании хранилища.
    using Engine = IStorageEngine<Entry>;
    using Slot = typename Engine::Slot;

public:
    using EnginePtr = std::unique_ptr<Engine>;

    // Этап 9: главный конструктор — все зависимости приходят снаружи:
    // движок, предел объёма, часы. Хранилище не знает, какой движок ему дали,
    // и не создаёт его само; собирает объекты точка сборки (src/compose.cpp).
    //
    // Этап 3: предел объёма — как maxmemory у Redis (MemoryBudget). Часы
    // хранилище не держит во владении: они живут у точки сборки дольше
    // хранилища, а по умолчанию это единственный на программу SteadyClock.
    explicit BasicStore(EnginePtr engine, std::optional<std::size_t> memory_limit = std::nullopt,
                        const IClock& clock = steady_clock())
        : engine_(std::move(engine)), budget_(memory_limit), clock_(&clock) {}

    // Удобные конструкторы — для тестов и примеров: движок по имени через
    // фабрику make_engine, объявленную в storage_engine.hpp. Движок по
    // умолчанию — из окружения (default_engine_kind): так весь набор тестов
    // проходит на каждом движке без правок. Это осознанно оставленная
    // скрытая зависимость (docs/audit.md, «Что оставлено»).
    BasicStore() : BasicStore(default_engine_kind()) {}
    explicit BasicStore(EngineKind engine) : BasicStore(make_engine<Entry>(engine)) {}
    explicit BasicStore(std::size_t memory_limit, EngineKind engine = default_engine_kind())
        : BasicStore(make_engine<Entry>(engine), memory_limit) {}

    // Копия — глубокая: движок клонируется (на копии стоит MSET), диапазонный
    // индекс копируется. Перемещение — передача владения движком.
    BasicStore(const BasicStore& other)
        : engine_(other.engine_->clone()), ordered_(other.ordered_), budget_(other.budget_),
          clock_(other.clock_), tick_(other.tick_) {}
    BasicStore& operator=(const BasicStore& other) {
        BasicStore copy(other);
        swap(*this, copy);
        return *this;
    }
    BasicStore(BasicStore&&) noexcept = default;
    BasicStore& operator=(BasicStore&&) noexcept = default;
    ~BasicStore() = default;

    [[nodiscard]] std::string_view engine_name() const noexcept { return engine_->name(); }
    // Число корзин движка — граница курсора SCAN; для тестов и docs/hashing.md.
    [[nodiscard]] std::size_t engine_bucket_count() const noexcept { return engine_->bucket_count(); }

    // Сильная гарантия: всё, что может бросить, — проверка предела и вставка —
    // происходит до изменения учёта занятой памяти. Бросок оставляет
    // хранилище прежним.
    //
    // Этап 5: каждая запись получает версию — следующее поколение ключа и
    // отметку логических часов хранилища. Версия лежит в той же записи
    // таблицы, что и значение: одна вставка вместо двух, и согласовывать
    // два контейнера при броске не нужно.
    //
    // Этап 8: две структуры — движок и диапазонный индекс, — и сильная
    // гарантия держится откатом: новый ключ сначала ложится в индекс, и если
    // движок бросит, ключ из индекса убирается; удаление из std::set не
    // бросает.
    void set(std::string key, V value, std::string author = "local") {
        const std::size_t added = footprint(key, value);
        const Entry* const existing = engine_->find(key);
        const std::size_t removed = existing == nullptr ? 0 : footprint(key, existing->value);
        const std::size_t next_used = budget_.after(removed, added);
        budget_.require(key, next_used);

        const std::uint64_t generation = existing == nullptr ? 1 : existing->version.generation + 1;
        Entry entry{std::move(value), RecordVersion{generation, tick_ + 1, std::move(author)}, clock_->now()};
        std::optional<typename Ordered::iterator> indexed;
        if (existing == nullptr) {
            indexed = ordered_.insert(key).first;   // может бросить — ничего не изменено
        }
        try {
            engine_->put(std::move(key), std::move(entry));
        } catch (...) {
            if (indexed) {
                ordered_.erase(*indexed);
            }
            throw;
        }
        budget_.commit(next_used);
        ++tick_;
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
        Entry* const found = engine_->find(key_view(key));
        if (found == nullptr) {
            throw KeyNotFoundError(std::string(key_view(key)));
        }
        budget_.require(std::string(key_view(key)), budget_.used() + growth);
        Entry& entry = *found;
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
        const Entry* const entry = engine_->find(key_view(key));
        if (entry == nullptr) {
            return std::nullopt;
        }
        return entry->value;
    }

    // Этап 7: доступ к значению без копии — для команд, которые только
    // читают. До этапа 7 GET, TYPE и LLEN шли через get и копировали
    // значение целиком, а для списка это копия всех его элементов ради
    // одного числа. Ссылка действительна, пока ключ не изменён и не удалён.
    //
    // Этап 8: и пока в хранилище ничего не вставлено — у открытой адресации
    // вставка может перенести все элементы.
    template <KeyLike K>
    [[nodiscard]] std::optional<std::reference_wrapper<const V>> lookup(const K& key) const {
        const Entry* const entry = engine_->find(key_view(key));
        if (entry == nullptr) {
            return std::nullopt;
        }
        return std::cref(entry->value);
    }

    template <KeyLike K>
    [[nodiscard]] bool contains(const K& key) const noexcept {
        return engine_->contains(key_view(key));
    }

    // Этап 9: сколько прошло с последней записи ключа — по часам хранилища
    // (IClock), как OBJECT IDLETIME у Redis. Первый потребитель часов; на
    // этапе 11 по ним же будет считаться TTL.
    template <KeyLike K>
    [[nodiscard]] std::optional<std::chrono::steady_clock::duration> idle_time(const K& key) const {
        const Entry* const entry = engine_->find(key_view(key));
        if (entry == nullptr) {
            return std::nullopt;
        }
        return clock_->now() - entry->touched;
    }

    template <KeyLike K>
    [[nodiscard]] std::optional<RecordVersion> version(const K& key) const {
        const Entry* const entry = engine_->find(key_view(key));
        if (entry == nullptr) {
            return std::nullopt;
        }
        return entry->version;
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
        const auto bounded = static_cast<std::ptrdiff_t>(std::min(limit, size()));
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
        //
        // Этап 8: ranges::copy — тоже один проход, без предварительного
        // подсчёта: алгоритм вместо цикла, и предикат вызывается столько же.
        std::vector<std::string> result;
        result.reserve(std::min(limit, size()));
        std::ranges::copy(selected, std::back_inserter(result));
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
        for (const std::string& key : *this) {
            if (glob_match(pattern, key)) {
                co_yield key;
            }
        }
    }

    // --- Этап 7: собственный итератор по ключам -------------------------
    //
    // Прямой (forward) итератор: многопроходный, копируемый, сравнимый. Отдаёт
    // только ключ: значение и версия — подробности хранилища, снаружи их
    // видно через get. Пять типов-членов — то, что смотрят
    // std::iterator_traits и концепции.
    //
    // Этап 8: итератор ходит по позициям движка — корзина и номер в ней, —
    // а не по итератору конкретного контейнера: движок за интерфейсом, и
    // какой он, итератор не знает. Конец — пустая позиция.
    //
    // Итератор действителен, пока таблица не перестраивается: вставка,
    // вызвавшая рехеширование, перекладывает элементы по другим корзинам у
    // любого движка. Поэтому обходить хранилище можно, только пока в него не
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

        [[nodiscard]] reference operator*() const noexcept { return engine_->key_at(*slot_); }
        [[nodiscard]] pointer operator->() const noexcept { return &engine_->key_at(*slot_); }

        Iterator& operator++() noexcept {
            slot_ = engine_->next(*slot_);
            return *this;
        }

        // Постфиксный инкремент возвращает копию до сдвига — это и отличает
        // forward-итератор от input: старую копию можно разыменовать.
        Iterator operator++(int) noexcept {
            Iterator before = *this;
            ++*this;
            return before;
        }

        // Номер корзины текущей позиции — на нём стоит курсор SCAN.
        [[nodiscard]] std::size_t bucket() const noexcept { return slot_->bucket; }
        [[nodiscard]] bool at_end() const noexcept { return !slot_; }

        // Позиции сравниваются без движка: итераторы одного хранилища.
        [[nodiscard]] friend bool operator==(const Iterator& a, const Iterator& b) noexcept {
            return a.slot_ == b.slot_;
        }

    private:
        friend class BasicStore;
        Iterator(const Engine* engine, std::optional<Slot> slot) noexcept : engine_(engine), slot_(slot) {}

        const Engine* engine_ = nullptr;
        std::optional<Slot> slot_;
    };

    [[nodiscard]] Iterator begin() const noexcept { return Iterator(engine_.get(), engine_->first_from(0)); }
    [[nodiscard]] Iterator end() const noexcept { return Iterator(engine_.get(), std::nullopt); }

    // --- Звёздочка этапа 7: ленивый просмотр со стражем ------------------
    //
    // scan(pattern) — диапазон ключей, подходящих под образец. Итератор —
    // input, а не forward: он однопроходный и некопируемый, а следующий
    // подходящий ключ ищет только при ++ — ничего не вычисляется заранее.
    // Конец — отдельный тип-страж ScanEnd, а не второй итератор: «конец»
    // здесь — не позиция, а условие «подходящих ключей больше нет», и
    // итератор проверяет его сам.
    struct ScanEnd {};

    class ScanIterator {
    public:
        using value_type = std::string;
        using difference_type = std::ptrdiff_t;

        ScanIterator(ScanIterator&&) noexcept = default;
        ScanIterator& operator=(ScanIterator&&) noexcept = default;

        [[nodiscard]] const std::string& operator*() const noexcept { return *position_; }

        ScanIterator& operator++() {
            ++position_;
            skip();
            return *this;
        }

        // У input-итератора постфиксный инкремент может ничего не
        // возвращать: копии нет, разыменовывать старую позицию нечем.
        void operator++(int) { ++*this; }

        [[nodiscard]] friend bool operator==(const ScanIterator& it, ScanEnd) noexcept {
            return it.position_.at_end();
        }

    private:
        friend class BasicStore;
        ScanIterator(Iterator first, std::string_view pattern) : position_(first), pattern_(pattern) { skip(); }

        // Сдвиг до ближайшего подходящего ключа — вся «ленивость» здесь.
        void skip() {
            while (!position_.at_end() && !glob_match(pattern_, *position_)) {
                ++position_;
            }
        }

        Iterator position_;
        std::string_view pattern_;   // образец живёт у вызывающего, как и диапазон
    };

    // Диапазон смотрит в таблицу и в строку образца: оба обязаны пережить
    // обход.
    [[nodiscard]] auto scan(std::string_view pattern) const {
        return std::ranges::subrange<ScanIterator, ScanEnd>(ScanIterator(begin(), pattern), ScanEnd{});
    }

    // --- Этап 8: SCAN с курсором ----------------------------------------
    //
    // Курсор — номер корзины, как в Redis. Страница — ключи из count корзин
    // подряд начиная с cursor, подходящие под образец, и курсор следующей
    // страницы; 0 — обход закончен. Клиент, которому нужно всё, повторяет
    // SCAN, пока не получит 0, и ни один вызов не держит хранилище дольше,
    // чем стоит обойти count корзин, — в этом отличие от KEYS.
    //
    // Целиком на алгоритмах: страница — диапазон итераторов хранилища от
    // первой позиции в корзине cursor до первой позиции в корзине stop;
    // filter — по образцу; ranges::copy — в результат. Ни одного ручного
    // цикла.
    //
    // Гарантия — как у Redis без его хитрости с обратным порядком битов: ключ,
    // который был в хранилище от первого до последнего вызова, попадёт в
    // ответ хотя бы раз, если между вызовами не было рехеширования. После
    // рехеширования ключи раскладываются по корзинам заново: у движка hash
    // (степень двойки, удвоение) ключ может встретиться дважды, но не
    // пропадёт; у std и open — может и пропасть (docs/hashing.md).
    struct ScanPage {
        std::size_t next_cursor = 0;
        std::vector<std::string> keys;
    };

    [[nodiscard]] ScanPage scan_page(std::size_t cursor, std::size_t count, std::string_view pattern) const {
        const std::size_t buckets = engine_->bucket_count();
        // Хотя бы одна корзина за вызов: иначе SCAN с count 0 возвращал бы
        // тот же курсор вечно.
        const std::size_t step = std::max<std::size_t>(count, 1);
        const std::size_t stop = cursor >= buckets ? buckets : cursor + std::min(step, buckets - cursor);
        const Iterator first(engine_.get(), engine_->first_from(cursor));
        const Iterator last(engine_.get(), engine_->first_from(stop));
        ScanPage page;
        page.next_cursor = stop >= buckets ? 0 : stop;
        std::ranges::copy(std::ranges::subrange(first, last) | std::views::filter([pattern](const std::string& key) {
                              return glob_match(pattern, key);
                          }),
                          std::back_inserter(page.keys));
        return page;
    }

    // --- Этап 8: KEYSRANGE по упорядоченному индексу --------------------
    //
    // Ключи от from до to включительно, в порядке строк, не больше limit.
    // Хеш-таблица порядка не хранит, и для диапазонов у хранилища есть
    // второй индекс — std::set ключей, красно-чёрное дерево. lower_bound и
    // upper_bound находят границы за O(log n), и между ними — ровно нужные
    // ключи, без обхода всей таблицы. Почему дерево сбалансировано и почему
    // вставка стоит не больше двух поворотов — docs/rbtree.md.
    [[nodiscard]] std::vector<std::string> keys_range(std::string_view from, std::string_view to,
                                                      std::size_t limit) const {
        if (to < from) {
            return {};
        }
        const auto first = ordered_.lower_bound(from);
        const auto last = ordered_.upper_bound(to);
        const auto bounded = static_cast<std::ptrdiff_t>(
            std::min<std::size_t>(limit, static_cast<std::size_t>(std::distance(first, last))));
        std::vector<std::string> result;
        result.reserve(static_cast<std::size_t>(bounded));
        std::ranges::copy(std::ranges::subrange(first, last) | std::views::take(bounded),
                          std::back_inserter(result));
        return result;
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
        a.engine_.swap(b.engine_);
        a.ordered_.swap(b.ordered_);
        std::swap(a.budget_, b.budget_);
        std::swap(a.clock_, b.clock_);
        std::swap(a.tick_, b.tick_);
    }

    template <KeyLike K>
    bool del(const K& key) {
        const std::string_view name = key_view(key);
        const Entry* const entry = engine_->find(name);
        if (entry == nullptr) {
            return false;
        }
        budget_.commit(budget_.after(name.size() + entry->value.size(), 0));
        // Сначала индекс: ключ name может смотреть в строку, которую
        // уничтожит удаление из движка.
        if (const auto it = ordered_.find(name); it != ordered_.end()) {
            ordered_.erase(it);
        }
        engine_->erase(name);
        return true;
    }

    [[nodiscard]] std::size_t used_bytes() const noexcept { return budget_.used(); }
    [[nodiscard]] std::optional<std::size_t> memory_limit() const noexcept { return budget_.limit(); }

    [[nodiscard]] std::size_t size() const noexcept { return engine_->size(); }
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    // Функтор принимается по значению — как это делают алгоритмы стандартной
    // библиотеки. Именно поэтому лямбда с захватом по значению и mutable не
    // выносит изменения наружу: меняется копия.
    //
    // Structured bindings из C++17: пара раскладывается на имена, а не
    // читается через first и second.
    //
    // Этап 8: обход — позициями движка, как и у итератора.
    template <class F>
    void for_each(F fn) const {
        for (std::optional<Slot> slot = engine_->first_from(0); slot; slot = engine_->next(*slot)) {
            fn(engine_->key_at(*slot), engine_->entry_at(*slot).value);
        }
    }

private:
    [[nodiscard]] static std::size_t footprint(const std::string& key, const V& value) noexcept {
        return key.size() + value.size();
    }

    // Учёт после изменения на месте: объём по фактическому размеру и новая
    // версия записи. Только арифметика и короткая строка автора.
    void account(Entry& entry, std::size_t before) {
        budget_.commit(budget_.after(before, entry.value.size()));
        entry.version = RecordVersion{entry.version.generation + 1, ++tick_, "local"};
        entry.touched = clock_->now();
    }

    // Этап 8: диапазонный индекс — те же ключи в порядке строк. Ключи в нём
    // хранятся второй раз; в учёт объёма они не входят — учёт приближённый с
    // этапа 3 и считает данные клиента, а не служебные структуры.
    using Ordered = std::set<std::string, std::less<>>;

    EnginePtr engine_;
    Ordered ordered_;
    MemoryBudget budget_;
    const IClock* clock_;     // монотонные часы — не во владении (этап 9)
    std::uint64_t tick_ = 0;  // логические часы версий: +1 на каждую запись
};

using Store = BasicStore<Value>;

}  // namespace kvs
