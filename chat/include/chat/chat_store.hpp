#pragma once

#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <generator>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "chat/chat_data.hpp"
#include "chat/clock.hpp"
#include "chat/concepts.hpp"
#include "chat/errors.hpp"
#include "chat/message.hpp"
#include "chat/message_id.hpp"
#include "chat/message_ids.hpp"
#include "chat/ring_buffer.hpp"
#include "chat/user.hpp"

namespace chat {

// Хранилище параметризовано типом элемента истории по одной практической
// причине: в тестах вместо MessagePtr подставляется инструментированный тип,
// считающий копирования и перемещения. Без этого утверждение «добавление
// временного сообщения не копирует» проверить нечем.
// Этап 5: что хранилище требует от элемента истории. Рассылка делает копии
// функцией duplicate — для unique_ptr<IMessage> это clone, для копируемых
// тестовых типов обычное копирование, — и переносит их в истории
// перемещением, которое бросать не должно. Концепция объявлена здесь, а не в
// concepts.hpp: ей нужна видимая перегрузка duplicate из message.hpp.
template <class M>
//
// Этап 7: и присваивание перемещением без исключений — полная история
// вытесняет самое старое сообщение присваиванием поверх него, и в фазе
// фиксации рассылки оно не должно бросать.
//
// Этап 8: и отметка времени — по ней строится индекс истории по времени
// (HISTORY SINCE и UNTIL). Для сообщения это время из его идентификатора,
// для тестовых типов — то, что они скажут.
concept HistoryItem = std::is_nothrow_move_constructible_v<M> &&
                      std::is_nothrow_move_assignable_v<M> && requires(const M& message) {
    { duplicate(message) } -> std::same_as<M>;
    { timestamp_of(message) } -> std::same_as<std::uint64_t>;
};

template <HistoryItem M>
class BasicChatStore {
public:
    // Этап 7: история — кольцевой буфер ограниченной ёмкости (ring_buffer.hpp,
    // выбор по замерам — docs/benchmarks.md).
    using History = RingBuffer<M>;

    // Этап 4: участники — наблюдатели, а не владельцы. Пользователями владеет
    // реестр (shared_ptr), комната хранит weak_ptr. Ушедший пользователь
    // исчезает из реестра, и комната его больше не удерживает.
    //
    // std::owner_less сравнивает weak_ptr по контрольному блоку, а не по
    // значению указателя, и сравнение остаётся корректным, даже когда объект
    // уже уничтожен. Поэтому истёкший weak_ptr можно хранить в set и найти,
    // чтобы удалить.
    using Members = std::set<std::weak_ptr<User>, std::owner_less<>>;

    // Этап 8: индекс истории по времени — красно-чёрное дерево (std::set)
    // пар «отметка времени, абсолютный номер сообщения». Номер делает ключ
    // уникальным: у двух сообщений одной миллисекунды разные номера. Пара
    // сравнивается лексикографически, поэтому сообщения одной миллисекунды
    // лежат в порядке отправки. Почему дерево сбалансировано — docs/rbtree.md.
    using TimeIndex = std::set<std::pair<std::uint64_t, std::size_t>>;

    // Комната — история, участники, тема и индекс по времени. Структура, а
    // не класс: её инварианты держит хранилище.
    struct Room {
        explicit Room(std::size_t history_capacity) : history(history_capacity) {}

        History history;
        Members members;
        std::string topic;
        TimeIndex by_time;   // ровно по записи на каждое сообщение в history
    };

    // Открытая адресация переносит комнаты перемещением при рехешировании, и
    // бросок посреди переноса оставил бы таблицу наполовину перестроенной.
    static_assert(std::is_nothrow_move_constructible_v<Room>);

    using Engine = IChatStore<Room>;

    // Сколько сообщений помнит комната по умолчанию.
    static constexpr std::size_t default_history_capacity = 1000;

    using EnginePtr = std::unique_ptr<Engine>;

    // Этап 9: главный конструктор — все зависимости приходят снаружи: данные
    // (IChatStore), ёмкость истории, часы. Хранилище не создаёт свои данные
    // само; собирает объекты точка сборки (src/compose.cpp). Часы не во
    // владении: они живут у точки сборки дольше хранилища, а по умолчанию это
    // единственные на программу SystemClocks.
    explicit BasicChatStore(EnginePtr engine, std::size_t history_capacity = default_history_capacity,
                            const IClock& clock = system_clocks())
        : engine_(std::move(engine)), history_capacity_(history_capacity), clock_(&clock), ids_(clock) {}

    // Удобные конструкторы — для тестов и примеров: данные по имени движка
    // через фабрику make_chat_store, объявленную в chat_data.hpp. Движок по
    // умолчанию — из окружения (default_engine_kind): так весь набор тестов
    // прогоняется на каждом движке без правок. Это осознанно оставленная
    // скрытая зависимость (docs/audit.md, «Что оставлено»).
    BasicChatStore() : BasicChatStore(default_engine_kind()) {}
    explicit BasicChatStore(EngineKind engine) : BasicChatStore(make_chat_store<Room>(engine)) {}
    explicit BasicChatStore(std::size_t history_capacity, EngineKind engine = default_engine_kind())
        : BasicChatStore(make_chat_store<Room>(engine), history_capacity) {}

    [[nodiscard]] std::size_t history_capacity() const noexcept { return history_capacity_; }
    [[nodiscard]] std::string_view engine_name() const noexcept { return engine_->name(); }

    bool create_room(std::string room) {
        if (engine_->find_room(room) != nullptr) {
            return false;
        }
        add_room(std::move(room));
        return true;
    }

    // --- Реестр пользователей ----------------------------------------------

    // Регистрация ника. make_shared: объект и контрольный блок — одна
    // аллокация. Обратная сторона для чата: память под User освободится,
    // только когда умрёт и последний weak_ptr, то есть когда истёкшую запись
    // уберёт purge_departed.
    std::shared_ptr<User> register_user(std::string nick) {
        if (engine_->find_user(nick) != nullptr) {
            throw NicknameTakenError(std::move(nick));
        }
        auto user = std::make_shared<User>(nick);
        user->touch(clock_->now());   // вход — тоже активность
        engine_->add_user(std::move(nick), user);
        return user;
    }

    // Реестр перестаёт владеть пользователем: на этапе 15 это отключение
    // клиента. Комнаты об этом не узнают и не должны: их weak_ptr истекают
    // сами, и is_member, members и рассылка ушедшего больше не видят.
    void release_user(std::string_view nick) { engine_->remove_user(nick); }

    [[nodiscard]] bool has_user(std::string_view nick) const { return engine_->find_user(nick) != nullptr; }

    // Пользователь по нику или nullptr.
    [[nodiscard]] std::shared_ptr<User> find_user(std::string_view nick) const {
        const std::shared_ptr<User>* const user = engine_->find_user(nick);
        return user == nullptr ? nullptr : *user;
    }

    // Смена ника. Комнаты хранят ссылку на объект User, а не строку, и смена
    // ника касается одного объекта и одной записи реестра.
    //
    // Сильная гарантия: копия ника для объекта и перенос записи реестра —
    // всё, что может бросить, — до изменения объекта. Перенос записи даёт
    // сильную гарантию сам (chat_data.hpp), а переименование объекта —
    // перемещение строки — не бросает.
    void rename(std::string_view from, std::string to) {
        if (from == to) {
            return;
        }
        if (engine_->find_user(to) != nullptr) {
            throw NicknameTakenError(std::move(to));
        }
        std::shared_ptr<User> user = find_user(from);
        if (!user) {
            throw UnknownUserError(std::string(from));
        }
        std::string nick_for_user = to;   // копия может бросить — ничего не изменено
        engine_->move_user(from, std::move(to));
        user->rename(std::move(nick_for_user));
    }

    [[nodiscard]] std::size_t user_count() const noexcept { return engine_->user_count(); }

    // --- Участники ----------------------------------------------------------

    // Вход в комнату. Несуществующая комната создаётся — в чате войти в
    // новую комнату и значит её завести. Возвращает false, если участник уже
    // был в комнате. Незарегистрированный ник — UnknownUserError.
    bool join(std::string_view room, std::string_view nick) {
        const std::shared_ptr<User> user = require_user(nick);
        // Единственное место, где комната создаётся неявно. Все остальные
        // операции ищут комнату и отсутствующую не создают.
        Room* target = engine_->find_room(room);
        if (target == nullptr) {
            target = &add_room(std::string(room));
        }
        purge(target->members);
        return target->members.insert(user).second;
    }

    // Выход. false — если комнаты нет или участника в ней не было.
    bool leave(std::string_view room, std::string_view nick) {
        Room* const target = engine_->find_room(room);
        const std::shared_ptr<User> user = find_user(nick);
        if (target == nullptr || !user) {
            return false;
        }
        purge(target->members);
        return target->members.erase(user) != 0;
    }

    [[nodiscard]] bool is_member(std::string_view room, std::string_view nick) const {
        const Room* const target = engine_->find_room(room);
        const std::shared_ptr<User> user = find_user(nick);
        return target != nullptr && user && target->members.contains(user);
    }

    // Живые участники, по алфавиту. Каждый удержан shared_ptr: пока вызывающий
    // держит результат — например, на время рассылки на этапе 15, —
    // участник не исчезнет посреди операции.
    //
    // Этап 8: алгоритмы вместо цикла. transform — lock каждого weak_ptr
    // (lock, а не expired и затем доступ: между ними объект мог бы умереть);
    // std::erase — идиома erase-remove одной функцией C++20: истёкшие дали
    // nullptr, и они уходят из вектора; sort с проекцией — по нику.
    [[nodiscard]] std::optional<std::vector<std::shared_ptr<User>>>
    members(std::string_view room) const {
        const Room* const target = engine_->find_room(room);
        if (target == nullptr) {
            return std::nullopt;
        }
        std::vector<std::shared_ptr<User>> alive;
        alive.reserve(target->members.size());
        std::ranges::transform(target->members, std::back_inserter(alive),
                               [](const std::weak_ptr<User>& member) { return member.lock(); });
        std::erase(alive, nullptr);
        std::ranges::sort(alive, {}, [](const std::shared_ptr<User>& user) -> const std::string& {
            return user->nick();
        });
        return alive;
    }

    // Сколько живых участников — count_if, без сбора вектора. Для /rooms:
    // там нужно число, а не состав.
    [[nodiscard]] std::size_t live_members(std::string_view room) const {
        const Room* const target = engine_->find_room(room);
        if (target == nullptr) {
            return 0;
        }
        return static_cast<std::size_t>(std::ranges::count_if(
            target->members, [](const std::weak_ptr<User>& member) { return !member.expired(); }));
    }

    // Сколько записей в составе комнаты, включая истёкшие. Нужен тестам —
    // чтобы увидеть, что очистка работает.
    [[nodiscard]] std::size_t member_entries(std::string_view room) const {
        const Room* const target = engine_->find_room(room);
        return target == nullptr ? 0 : target->members.size();
    }

    // Очистка истёкших записей во всех комнатах. Без неё составы растут на
    // каждого ушедшего: объекты освобождены, а записи остались. join и
    // leave чистят свою комнату сами; эта функция — для всех сразу.
    std::size_t purge_departed() {
        std::size_t removed = 0;
        engine_->for_each_room([&removed](const std::string&, Room& room) { removed += purge(room.members); });
        return removed;
    }

    // --- Сообщения ----------------------------------------------------------

    // Добавление сообщения. Комнаты создаёт только /join, и отправка
    // в несуществующую комнату — UnknownRoomError.
    //
    // Этап 8: сообщение попадает и в историю, и в индекс по времени, и оба
    // должны измениться вместе. Сильная гарантия по той же схеме, что у
    // рассылки: подготовка (запас ёмкости, узел индекса) может бросить, но
    // не видна снаружи; фиксация не бросает.
    void post(std::string_view room, M message) {
        Room& target = require_room(room);
        target.history.reserve_one_more();               // может бросить — ничего не изменено
        index_entry(target, message);                    // может бросить — ничего не изменено
        commit_one(target, std::move(message));
    }

    // Этап 6, домашнее задание: исключение участника и уведомление об этом
    // — одна операция. Сильная гарантия: всё, что может бросить, — поиск,
    // проверки, запас ёмкости истории и узел индекса — до первого изменения.
    // Дальше erase (сравнение owner_less не бросает) и фиксация, которая не
    // бросает.
    //
    // Почему не leave и затем post: бросок post между ними оставил бы
    // участника исключённым без следа в истории. На этапе 11, когда
    // хранилище станет общим для потоков, одна операция — это и один захват
    // мьютекса: между исключением и уведомлением никто не вклинится.
    void kick(std::string_view room, std::string_view nick, M notice) {
        Room& target = require_room(room);
        const std::shared_ptr<User> user = find_user(nick);
        if (!user || !target.members.contains(user)) {
            throw NotMemberError(std::string(room), std::string(nick));
        }
        target.history.reserve_one_more();
        index_entry(target, notice);
        target.members.erase(user);
        commit_one(target, std::move(notice));
    }

    // Рассылка в несколько комнат с сильной гарантией: сообщение появляется
    // либо во всех комнатах, либо ни в одной.
    //
    // Всё, что может бросить, делается в фазе подготовки, и изменяет только
    // то, что не видно снаружи: поиск комнат, копии сообщения, запас ёмкости
    // в историях и — с этапа 8 — узлы индекса по времени. Узел индекса
    // виден поиску по времени, но указывает на номер, которого ещё нет в
    // истории, и при неудаче подготовки удаляется (erase по итератору не
    // бросает). Фаза фиксации бросить не может — это проверяет компилятор.
    //
    // Полная копия всех комнат с последующим swap дала бы ту же гарантию
    // проще, но ценой копирования всей истории чата на каждое сообщение.
    // Здесь копируется только само сообщение — по разу на комнату.
    // Сравнение подходов — в docs/errors.md.
    void post_many(const std::vector<std::string_view>& rooms, M message) {
        // Фиксация переносит копии в истории перемещением. Бросающее
        // перемещение сделало бы точку невозврата точкой частичной записи.
        static_assert(std::is_nothrow_move_constructible_v<M>,
                      "post_many needs a message type with a noexcept move constructor");
        if (rooms.empty()) {
            return;
        }

        // Подготовка, шаг 1: все комнаты существуют, и ни одна не названа
        // дважды. Этап 8: поиск повтора — ranges::contains из C++23.
        std::vector<Room*> targets;
        targets.reserve(rooms.size());
        for (const std::string_view room : rooms) {
            Room* const target = &require_room(room);
            // Повтор разбор отсекает раньше; здесь он нарушил бы расчёт
            // ёмкости ниже — запас на одно сообщение, а пришло бы два.
            if (std::ranges::contains(targets, target)) {
                throw ChatError("room listed twice: " + std::string(room));
            }
            targets.push_back(target);
        }

        // Шаг 2: копии сообщения — по одной на каждую комнату, кроме
        // последней: ей достаётся оригинал. Здесь бросают копирования.
        // Этап 8: generate_n вместо цикла со счётчиком.
        std::vector<M> copies;
        copies.reserve(targets.size());
        std::ranges::generate_n(std::back_inserter(copies), static_cast<std::ptrdiff_t>(targets.size() - 1),
                                [&message] { return duplicate(message); });
        copies.push_back(std::move(message));

        // Шаг 3: запас ёмкости, чтобы push_back при фиксации не выделял
        // память. Ёмкость — не наблюдаемое состояние.
        std::ranges::for_each(targets, [](Room* target) { target->history.reserve_one_more(); });

        // Шаг 4: узлы индекса по времени. Каждая вставка может бросить; тогда
        // уже вставленные удаляются, и индекс прежний.
        std::vector<typename TimeIndex::iterator> indexed;
        indexed.reserve(targets.size());
        try {
            for (std::size_t i = 0; i < targets.size(); ++i) {
                indexed.push_back(index_entry(*targets[i], copies[i]));
            }
        } catch (...) {
            for (std::size_t i = 0; i < indexed.size(); ++i) {
                targets[i]->by_time.erase(indexed[i]);
            }
            throw;
        }

        commit(targets, copies);
    }

    // Этап 2: вместо сырого указателя — std::optional. Внутри — ссылка через
    // reference_wrapper: std::optional<T&> стандартом не предусмотрен, а
    // копировать историю на каждый запрос недопустимо.
    //
    // Этап 5: имя комнаты — любой NameLike: строка, string_view, литерал.
    template <NameLike N>
    [[nodiscard]] std::optional<std::reference_wrapper<const History>>
    history(const N& room) const {
        const Room* const target = engine_->find_room(name_view(room));
        if (target == nullptr) {
            return std::nullopt;
        }
        return std::cref(target->history);
    }

    // Изменяющий доступ к сообщению нужен команде REACT.
    //
    // Этап 7: index — абсолютный номер сообщения в комнате. Вытесненное
    // сообщение (номер меньше evicted) и ещё не написанное — пустой
    // optional.
    [[nodiscard]] std::optional<std::reference_wrapper<M>>
    message_at(std::string_view room, std::size_t index) {
        Room* const target = engine_->find_room(room);
        if (target == nullptr || !holds(target->history, index)) {
            return std::nullopt;
        }
        return std::ref(target->history[index - target->history.evicted()]);
    }

    [[nodiscard]] std::optional<std::reference_wrapper<const M>>
    message_at(std::string_view room, std::size_t index) const {
        const Room* const target = engine_->find_room(room);
        if (target == nullptr || !holds(target->history, index)) {
            return std::nullopt;
        }
        return std::cref(target->history[index - target->history.evicted()]);
    }

    [[nodiscard]] std::optional<std::string> topic(std::string_view room) const {
        const Room* const target = engine_->find_room(room);
        if (target == nullptr) {
            return std::nullopt;
        }
        return target->topic;
    }

    bool set_topic(std::string_view room, std::string topic) {
        Room* const target = engine_->find_room(room);
        if (target == nullptr) {
            return false;
        }
        target->topic = std::move(topic);
        return true;
    }

    template <NameLike N>
    [[nodiscard]] bool contains(const N& room) const {
        return engine_->find_room(name_view(room)) != nullptr;
    }

    // --- Этап 5: история через ranges --------------------------------------
    //
    // Последние limit сообщений, удовлетворяющих предикату, в хронологическом
    // порядке, вместе с номерами в истории. Конвейер:
    //
    //     enumerate | reverse | filter | take
    //
    // enumerate из C++23 приклеивает к каждому сообщению его номер; reverse
    // идёт от новых к старым; filter отбирает; take останавливает обход, как
    // только набралось limit. Ничего не материализуется до take: на комнате
    // из тысячи сообщений при limit 10 предикат вызывается немногим больше
    // десяти раз. Тест History.PipelineIsLazy проверяет это счётчиком.
    //
    // Возвращаются ссылки внутрь истории: пока результат жив, комнату нельзя
    // менять — тот же урок, что со string_view на этапе 2.
    struct Entry {
        std::size_t index;
        std::reference_wrapper<const M> message;
    };

    template <NameLike N, std::predicate<const M&> Predicate>
    [[nodiscard]] std::optional<std::vector<Entry>> history_where(const N& room,
                                                                  Predicate predicate,
                                                                  std::size_t limit) const {
        const Room* const target = engine_->find_room(name_view(room));
        if (target == nullptr) {
            return std::nullopt;
        }
        // Предел обрезается по размеру истории до того, как попасть в take.
        // take принимает знаковую разность итераторов, и size_t больше
        // PTRDIFF_MAX превратился бы в отрицательное число — нарушение
        // предусловия counted_iterator. Это нашёл фаззер: /history #general
        // 18446744073709551615 ронял отладочную сборку на проверке
        // библиотеки, а в обычной сборке был бы неопределённым поведением.
        const History& history = target->history;
        const auto bounded = static_cast<std::ptrdiff_t>(std::min(limit, history.size()));
        auto selected = history | std::views::enumerate | std::views::reverse |
                        std::views::filter([&predicate](const auto& pair) {
                            return std::invoke(predicate, std::get<1>(pair));
                        }) |
                        std::views::take(bounded);
        // Материализация — обычным циклом, а не std::ranges::to: ranges::to
        // для прямого диапазона проходит его дважды — сначала чтобы узнать
        // размер, — и предикат вызывался бы почти вдвое чаще.
        std::vector<Entry> result;
        for (const auto& [index, message] : selected) {
            // Этап 7: номер — абсолютный: позиция в буфере плюс вытесненные.
            result.push_back(Entry{history.evicted() + static_cast<std::size_t>(index), std::cref(message)});
        }
        std::ranges::reverse(result);   // обратно в хронологический порядок
        return result;
    }

    // --- Этап 8: история по времени ----------------------------------------
    //
    // Сообщения с отметкой времени от since до until включительно — через
    // индекс по времени: lower_bound и upper_bound находят границы за
    // O(log n), между ними — ровно нужные сообщения, без обхода истории.
    // Earliest — первые limit сообщений окна (HISTORY SINCE: «что было с
    // такого-то момента»), Latest — последние limit (HISTORY UNTIL: «что
    // было до такого-то момента»). Результат — в хронологическом порядке.
    enum class Window { Earliest, Latest };

    template <NameLike N>
    [[nodiscard]] std::optional<std::vector<Entry>> history_between(const N& room, std::uint64_t since,
                                                                    std::uint64_t until, std::size_t limit,
                                                                    Window window) const {
        const Room* const target = engine_->find_room(name_view(room));
        if (target == nullptr) {
            return std::nullopt;
        }
        std::vector<Entry> result;
        if (until < since) {
            return result;
        }
        const TimeIndex& index = target->by_time;
        const History& history = target->history;
        const auto first = index.lower_bound({since, 0});
        const auto last = index.upper_bound({until, std::numeric_limits<std::size_t>::max()});
        const auto span = std::ranges::subrange(first, last);
        const auto bounded = static_cast<std::ptrdiff_t>(std::min(limit, index.size()));   // как в history_where
        const auto to_entry = [&history](const std::pair<std::uint64_t, std::size_t>& key) {
            return Entry{key.second, std::cref(history[key.second - history.evicted()])};
        };
        if (window == Window::Earliest) {
            std::ranges::transform(span | std::views::take(bounded), std::back_inserter(result), to_entry);
        } else {
            std::ranges::transform(span | std::views::reverse | std::views::take(bounded),
                                   std::back_inserter(result), to_entry);
            std::ranges::reverse(result);
        }
        return result;
    }

    // Звёздочка этапа 5: история генератором, от новых к старым. Корутина
    // отдаёт сообщения по одному и приостанавливается между ними. Если обход
    // прерван, кадр корутины уничтожается в точке приостановки, и её
    // локальные объекты разрушаются как положено: RAII работает и внутри
    // корутины. Плата — кадр корутины в куче.
    //
    // Генератор смотрит в историю: обходить его можно, только пока комната
    // не меняется. Несуществующая комната — пустая последовательность.
    [[nodiscard]] std::generator<const M&> newest_first(std::string room) const {
        const Room* const target = engine_->find_room(room);
        if (target == nullptr) {
            co_return;
        }
        for (const M& message : target->history | std::views::reverse) {
            co_yield message;
        }
    }

    // Идентификатор для нового сообщения: следующий номер в общей
    // последовательности чата и время по системным часам — его видит
    // пользователь, и по нему работают /since и /until. Этап 9: выдаёт их
    // MessageIdSource (message_ids.hpp); хранилище только передаёт запрос.
    [[nodiscard]] MessageId next_message_id(std::string author) { return ids_.next(std::move(author)); }

    // --- Этап 9: активность пользователя ------------------------------------
    //
    // Отметить, что пользователь только что что-то сделал. Время — монотонное
    // (IClock::now()): простой — интервал. Неизвестный ник — ничего: отмечать
    // некого. Зовёт сервер на каждую команду; на этапе 11 по этой отметке
    // будут отключаться неактивные сессии.
    void touch(std::string_view nick) noexcept {
        if (const std::shared_ptr<User>* const user = engine_->find_user(nick)) {
            (*user)->touch(clock_->now());
        }
    }

    // Сколько пользователь бездействует; неизвестный ник — пусто.
    [[nodiscard]] std::optional<std::chrono::steady_clock::duration> idle_time(std::string_view nick) const {
        const std::shared_ptr<User>* const user = engine_->find_user(nick);
        if (user == nullptr) {
            return std::nullopt;
        }
        return clock_->now() - (*user)->last_active();
    }

    [[nodiscard]] std::size_t room_count() const noexcept { return engine_->room_count(); }
    [[nodiscard]] bool empty() const noexcept { return engine_->room_count() == 0; }

    // Функтор принимается по значению — как это делают алгоритмы стандартной
    // библиотеки. Именно поэтому лямбда с захватом по значению и mutable не
    // выносит изменения наружу: меняется копия.
    //
    // Этап 8: обход — по упорядоченному индексу имён, поэтому по алфавиту
    // на любом движке.
    template <class F>
    void for_each_room(F fn) const {
        std::ranges::for_each(room_order_, [&](const std::string& name) {
            fn(name, engine_->find_room(name)->history);
        });
    }

    // --- Этап 8: /rooms с курсором -----------------------------------------
    //
    // Страница — не больше count имён по алфавиту строго после after (пусто
    // — с начала) и признак, что за страницей есть ещё. Курсор — имя
    // последней выданной комнаты: клиент передаёт его в следующий запрос.
    // Целиком на алгоритмах: upper_bound — начало страницы, take — её
    // размер, copy — в результат.
    //
    // Курсор-имя надёжнее курсора-номера: комнаты, созданные между вызовами,
    // не сдвигают страницы, и ни одна существующая комната не пропадёт и не
    // повторится. Для сравнения — курсор-номер корзины у SCAN в Redis.
    struct RoomPage {
        std::vector<std::string> names;
        bool more = false;
    };

    [[nodiscard]] RoomPage room_page(std::optional<std::string_view> after, std::size_t count) const {
        const auto first = after ? room_order_.upper_bound(*after) : room_order_.begin();
        const auto available = static_cast<std::size_t>(std::ranges::distance(first, room_order_.end()));
        const std::size_t taken = std::min(count, available);
        RoomPage page;
        page.names.reserve(taken);
        std::ranges::copy(std::ranges::subrange(first, room_order_.end()) |
                              std::views::take(static_cast<std::ptrdiff_t>(taken)),
                          std::back_inserter(page.names));
        page.more = taken < available;
        return page;
    }

private:
    [[nodiscard]] std::shared_ptr<User> require_user(std::string_view nick) const {
        std::shared_ptr<User> user = find_user(nick);
        if (!user) {
            throw UnknownUserError(std::string(nick));
        }
        return user;
    }

    Room& require_room(std::string_view room) {
        Room* const target = engine_->find_room(room);
        if (target == nullptr) {
            throw UnknownRoomError(std::string(room));
        }
        return *target;
    }

    // Новая комната — в двух местах: в упорядоченном индексе имён и в
    // движке. Сначала индекс: если бросит он, ничего не изменено; если
    // бросит движок, имя убирается из индекса по итератору — erase по
    // итератору не бросает. Предусловие — комнаты ещё нет.
    Room& add_room(std::string name) {
        const auto ordered = room_order_.insert(name).first;
        try {
            return engine_->add_room(std::move(name), Room(history_capacity_));
        } catch (...) {
            room_order_.erase(ordered);
            throw;
        }
    }

    // Удаление истёкших записей — std::erase_if из C++20. Ошибочный вариант
    // цикла — erase(it) и затем ++it — разобран в tools/invalidation_demo.cpp.
    static std::size_t purge(Members& members) {
        return std::erase_if(members, [](const std::weak_ptr<User>& member) { return member.expired(); });
    }

    [[nodiscard]] static bool holds(const History& history, std::size_t index) noexcept {
        return index >= history.evicted() && index - history.evicted() < history.size();
    }

    // Узел индекса для сообщения, которое станет следующим в комнате. Его
    // абсолютный номер — вытесненные плюс размер: если история полна, после
    // вставки вытесненных станет на одно больше, а размер — тот же.
    static typename TimeIndex::iterator index_entry(Room& room, const M& message) {
        return room.by_time.emplace(timestamp_of(message), room.history.evicted() + room.history.size()).first;
    }

    // Фиксация одного сообщения. Ёмкость запасена, узел индекса вставлен.
    // Если история полна, самое старое сообщение будет вытеснено — его
    // узел уходит из индекса (find и erase по итератору не бросают).
    static void commit_one(Room& room, M message) noexcept {
        if (room.history.full()) {
            const auto oldest = room.by_time.find({timestamp_of(room.history.front()), room.history.evicted()});
            room.by_time.erase(oldest);
        }
        room.history.push_back(std::move(message));
    }

    // Фиксация рассылки. noexcept здесь — контракт: ёмкость запасена,
    // перемещение не бросает, значит не бросает и push_back. Если контракт
    // всё же нарушен, программа завершится через std::terminate. Это лучше,
    // чем чат, в котором часть участников видит сообщение, а часть нет.
    static void commit(const std::vector<Room*>& targets, std::vector<M>& copies) noexcept {
        for (std::size_t i = 0; i < targets.size(); ++i) {
            commit_one(*targets[i], std::move(copies[i]));
        }
    }

    std::unique_ptr<Engine> engine_;
    // Этап 8: упорядоченный индекс имён комнат — красно-чёрное дерево.
    // /rooms выдаёт комнаты по алфавиту страницами, и на этапе 7 ради этого
    // комнаты лежали в std::map. Теперь таблица комнат — хеш-таблица движка
    // (поиск на каждое сообщение), а порядок держит этот индекс: вторая
    // копия каждого имени ради быстрых обоих запросов. Комнаты не удаляются,
    // поэтому индекс меняется только в add_room.
    std::set<std::string, std::less<>> room_order_;
    std::size_t history_capacity_ = default_history_capacity;
    const IClock* clock_;     // этап 9: часы — не во владении
    MessageIdSource ids_;     // этап 9: номера и отметки сообщений
};

using ChatStore = BasicChatStore<MessagePtr>;
using History = ChatStore::History;

}  // namespace chat
