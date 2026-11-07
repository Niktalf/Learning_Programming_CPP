#pragma once

#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <generator>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "chat/concepts.hpp"
#include "chat/errors.hpp"
#include "chat/message.hpp"
#include "chat/message_id.hpp"
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
concept HistoryItem = std::is_nothrow_move_constructible_v<M> && requires(const M& message) {
    { duplicate(message) } -> std::same_as<M>;
};

template <HistoryItem M>
class BasicChatStore {
public:
    using History = std::vector<M>;

    // Этап 4: участники — наблюдатели, а не владельцы. Пользователями владеет
    // реестр (shared_ptr), комната хранит weak_ptr. Ушедший пользователь
    // исчезает из реестра, и комната его больше не удерживает.
    //
    // std::owner_less сравнивает weak_ptr по контрольному блоку, а не по
    // значению указателя, и сравнение остаётся корректным, даже когда объект
    // уже уничтожен. Поэтому истёкший weak_ptr можно хранить в set и найти,
    // чтобы удалить.
    using Members = std::set<std::weak_ptr<User>, std::owner_less<>>;

    // Комната — история, участники и тема. Структура, а не класс: инвариантов,
    // которые надо охранять, у неё нет, их держит хранилище.
    struct Room {
        History history;
        Members members;
        std::string topic;
    };

    bool create_room(std::string room) {
        // try_emplace из C++17: значение конструируется только при вставке.
        return rooms_.try_emplace(std::move(room)).second;
    }

    // --- Реестр пользователей ----------------------------------------------

    // Регистрация ника. make_shared: объект и контрольный блок — одна
    // аллокация. Обратная сторона для чата: память под User освободится,
    // только когда умрёт и последний weak_ptr, то есть когда истёкшую запись
    // уберёт purge_departed.
    std::shared_ptr<User> register_user(std::string nick) {
        if (users_.count(nick) != 0) {
            throw NicknameTakenError(std::move(nick));
        }
        auto user = std::make_shared<User>(nick);
        users_.emplace(std::move(nick), user);
        return user;
    }

    // Реестр перестаёт владеть пользователем: на этапе 15 это отключение
    // клиента. Комнаты об этом не узнают и не должны: их weak_ptr истекают
    // сами, и is_member, members и рассылка ушедшего больше не видят.
    void release_user(std::string_view nick) {
        if (const auto it = users_.find(nick); it != users_.end()) {
            users_.erase(it);
        }
    }

    [[nodiscard]] bool has_user(std::string_view nick) const {
        return users_.find(nick) != users_.end();
    }

    // Пользователь по нику или nullptr.
    [[nodiscard]] std::shared_ptr<User> find_user(std::string_view nick) const {
        const auto it = users_.find(nick);
        return it == users_.end() ? nullptr : it->second;
    }

    // Смена ника. На этапе 3 это была операция над всеми комнатами: ник
    // приходилось заменить в составе каждой. Теперь комнаты хранят ссылку
    // на объект User, а не строку, и смена ника касается одного объекта и
    // одной записи реестра. Ни одна комната не меняется.
    //
    // Сильная гарантия: всё, что выделяет память, — до извлечения узла.
    // Дальше только перемещения строк и перенос узла map (extract/insert из
    // C++17), а он не выделяет памяти; бросить может лишь сравнение ключей,
    // а сравнение строк не бросает.
    void rename(std::string_view from, std::string to) {
        if (from == to) {
            return;
        }
        if (users_.count(to) != 0) {
            throw NicknameTakenError(std::move(to));
        }
        const auto it = users_.find(from);
        if (it == users_.end()) {
            throw UnknownUserError(std::string(from));
        }
        std::string nick_for_user = to;   // копия может бросить — ничего не изменено

        auto node = users_.extract(it);
        node.key() = std::move(to);
        node.mapped()->rename(std::move(nick_for_user));
        users_.insert(std::move(node));
    }

    [[nodiscard]] std::size_t user_count() const noexcept { return users_.size(); }

    // --- Участники ----------------------------------------------------------

    // Вход в комнату. Несуществующая комната создаётся — в чате войти в
    // новую комнату и значит её завести. Возвращает false, если участник уже
    // был в комнате. Незарегистрированный ник — UnknownUserError.
    bool join(std::string_view room, std::string_view nick) {
        const std::shared_ptr<User> user = require_user(nick);
        Room& target = rooms_.try_emplace(std::string(room)).first->second;
        purge(target.members);
        return target.members.insert(user).second;
    }

    // Выход. false — если комнаты нет или участника в ней не было.
    bool leave(std::string_view room, std::string_view nick) {
        const auto it = rooms_.find(room);
        const std::shared_ptr<User> user = find_user(nick);
        if (it == rooms_.end() || !user) {
            return false;
        }
        purge(it->second.members);
        return it->second.members.erase(user) != 0;
    }

    [[nodiscard]] bool is_member(std::string_view room, std::string_view nick) const {
        const auto it = rooms_.find(room);
        const std::shared_ptr<User> user = find_user(nick);
        return it != rooms_.end() && user && it->second.members.count(user) != 0;
    }

    // Живые участники, по алфавиту. Каждый удержан shared_ptr: пока вызывающий
    // держит результат — например, на время рассылки на этапе 15, —
    // участник не исчезнет посреди операции. weak_ptr не запрещает владеть,
    // он запрещает владеть постоянно.
    [[nodiscard]] std::optional<std::vector<std::shared_ptr<User>>>
    members(std::string_view room) const {
        const auto it = rooms_.find(room);
        if (it == rooms_.end()) {
            return std::nullopt;
        }
        std::vector<std::shared_ptr<User>> alive;
        for (const std::weak_ptr<User>& member : it->second.members) {
            // lock, а не expired с последующим доступом: между проверкой и
            // доступом объект мог бы умереть. lock делает и то и другое разом.
            if (std::shared_ptr<User> user = member.lock()) {
                alive.push_back(std::move(user));
            }
        }
        std::sort(alive.begin(), alive.end(),
                  [](const auto& a, const auto& b) { return a->nick() < b->nick(); });
        return alive;
    }

    // Сколько записей в составе комнаты, включая истёкшие. Нужен тестам —
    // чтобы увидеть, что очистка работает.
    [[nodiscard]] std::size_t member_entries(std::string_view room) const {
        const auto it = rooms_.find(room);
        return it == rooms_.end() ? 0 : it->second.members.size();
    }

    // Очистка истёкших записей во всех комнатах. Без неё составы растут на
    // каждого ушедшего: объекты освобождены, а записи остались. join и
    // leave чистят свою комнату сами; эта функция — для всех сразу.
    std::size_t purge_departed() {
        std::size_t removed = 0;
        for (auto& [name, room] : rooms_) {
            removed += purge(room.members);
        }
        return removed;
    }

    // --- Сообщения ----------------------------------------------------------

    // Добавление сообщения. Комнаты создаёт только /join, и отправка
    // в несуществующую комнату — UnknownRoomError.
    void post(std::string_view room, M message) {
        const auto it = rooms_.find(room);
        if (it == rooms_.end()) {
            throw UnknownRoomError(std::string(room));
        }
        it->second.history.push_back(std::move(message));
    }

    // Этап 6, домашнее задание: исключение участника и уведомление об этом
    // — одна операция. Сильная гарантия: всё, что может бросить, — поиск,
    // проверки и запас ёмкости истории — до первого изменения. Дальше
    // erase (сравнение owner_less не бросает) и push_back в запасённую
    // ёмкость с перемещением, которое не бросает.
    //
    // Почему не leave и затем post: бросок post между ними оставил бы
    // участника исключённым без следа в истории. На этапе 11, когда
    // хранилище станет общим для потоков, одна операция — это и один захват
    // мьютекса: между исключением и уведомлением никто не вклинится.
    void kick(std::string_view room, std::string_view nick, M notice) {
        static_assert(std::is_nothrow_move_constructible_v<M>,
                      "kick needs a message type with a noexcept move constructor");
        const auto it = rooms_.find(room);
        if (it == rooms_.end()) {
            throw UnknownRoomError(std::string(room));
        }
        Room& target = it->second;
        const std::shared_ptr<User> user = find_user(nick);
        if (!user || target.members.count(user) == 0) {
            throw NotMemberError(std::string(room), std::string(nick));
        }
        reserve_one_more(target.history);   // может бросить — ничего не изменено
        target.members.erase(user);
        target.history.push_back(std::move(notice));
    }

    // Рассылка в несколько комнат с сильной гарантией: сообщение появляется
    // либо во всех комнатах, либо ни в одной.
    //
    // Всё, что может бросить, делается в фазе подготовки, и изменяет только
    // то, что не видно снаружи: поиск комнат, копии сообщения, запас ёмкости
    // в историях. Фаза фиксации бросить не может — это проверяет компилятор.
    //
    // Полная копия всех комнат с последующим swap дала бы ту же гарантию
    // проще, но ценой копирования всей истории чата на каждое сообщение.
    // Здесь копируется только само сообщение — по разу на комнату.
    // Сравнение подходов — в docs/errors.md.
    void post_many(const std::vector<std::string_view>& rooms, M message) {
        // Фиксация переносит копии в истории перемещением. Бросающее
        // перемещение сделало бы точку невозврата точкой частичной записи:
        // первые комнаты получили бы сообщение, остальные нет.
        static_assert(std::is_nothrow_move_constructible_v<M>,
                      "post_many needs a message type with a noexcept move constructor");
        if (rooms.empty()) {
            return;
        }

        // Подготовка, шаг 1: все комнаты существуют.
        std::vector<History*> targets;
        targets.reserve(rooms.size());
        for (const std::string_view room : rooms) {
            const auto it = rooms_.find(room);
            if (it == rooms_.end()) {
                throw UnknownRoomError(std::string(room));
            }
            History* const history = &it->second.history;
            // Повтор разбор отсекает раньше; здесь он нарушил бы расчёт
            // ёмкости ниже — запас на одно сообщение, а пришло бы два.
            if (std::find(targets.begin(), targets.end(), history) != targets.end()) {
                throw ChatError("room listed twice: " + std::string(room));
            }
            targets.push_back(history);
        }

        // Шаг 2: копии сообщения — по одной на каждую комнату, кроме
        // последней: ей достаётся оригинал. Здесь бросают копирования.
        // Этап 4: сообщение лежит в unique_ptr, который не копируется, и
        // копию делает duplicate — для сообщения это clone. Без этого шага
        // рассылка просто не собралась бы.
        std::vector<M> copies;
        copies.reserve(targets.size());
        for (std::size_t i = 0; i + 1 < targets.size(); ++i) {
            copies.push_back(duplicate(message));
        }
        copies.push_back(std::move(message));

        // Шаг 3: запас ёмкости, чтобы push_back при фиксации не выделял
        // память. Ёмкость — не наблюдаемое состояние: история с запасом
        // содержит те же сообщения, поэтому бросок здесь гарантию не портит.
        for (History* const history : targets) {
            reserve_one_more(*history);
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
        const auto it = rooms_.find(name_view(room));
        if (it == rooms_.end()) {
            return std::nullopt;
        }
        return std::cref(it->second.history);
    }

    // Изменяющий доступ к сообщению нужен команде REACT.
    [[nodiscard]] std::optional<std::reference_wrapper<M>>
    message_at(std::string_view room, std::size_t index) {
        const auto it = rooms_.find(room);
        if (it == rooms_.end() || index >= it->second.history.size()) {
            return std::nullopt;
        }
        return std::ref(it->second.history[index]);
    }

    [[nodiscard]] std::optional<std::reference_wrapper<const M>>
    message_at(std::string_view room, std::size_t index) const {
        const auto it = rooms_.find(room);
        if (it == rooms_.end() || index >= it->second.history.size()) {
            return std::nullopt;
        }
        return std::cref(it->second.history[index]);
    }

    [[nodiscard]] std::optional<std::string> topic(std::string_view room) const {
        const auto it = rooms_.find(room);
        if (it == rooms_.end()) {
            return std::nullopt;
        }
        return it->second.topic;
    }

    bool set_topic(std::string_view room, std::string topic) {
        const auto it = rooms_.find(room);
        if (it == rooms_.end()) {
            return false;
        }
        it->second.topic = std::move(topic);
        return true;
    }

    template <NameLike N>
    [[nodiscard]] bool contains(const N& room) const {
        return rooms_.find(name_view(room)) != rooms_.end();
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
    // views::reverse требует двунаправленного диапазона. vector таким
    // является; на этапе 7 история переедет в кольцевой буфер, и его
    // итератор придётся сделать двунаправленным, иначе конвейер не соберётся.
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
        const auto it = rooms_.find(name_view(room));
        if (it == rooms_.end()) {
            return std::nullopt;
        }
        // Предел обрезается по размеру истории до того, как попасть в take.
        // take принимает знаковую разность итераторов, и size_t больше
        // PTRDIFF_MAX превратился бы в отрицательное число — нарушение
        // предусловия counted_iterator. Это нашёл фаззер: /history #general
        // 18446744073709551615 ронял отладочную сборку на проверке
        // библиотеки, а в обычной сборке был бы неопределённым поведением.
        const History& history = it->second.history;
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
            result.push_back(Entry{static_cast<std::size_t>(index), std::cref(message)});
        }
        std::ranges::reverse(result);   // обратно в хронологический порядок
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
        const auto it = rooms_.find(room);
        if (it == rooms_.end()) {
            co_return;
        }
        for (const M& message : it->second.history | std::views::reverse) {
            co_yield message;
        }
    }

    // Идентификатор для нового сообщения: следующий номер в общей
    // последовательности чата и текущее время.
    [[nodiscard]] MessageId next_message_id(std::string author) {
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch());
        return MessageId{++seq_, static_cast<std::uint64_t>(now.count()), std::move(author)};
    }

    [[nodiscard]] std::size_t room_count() const noexcept { return rooms_.size(); }
    [[nodiscard]] bool empty() const noexcept { return rooms_.empty(); }

    // Функтор принимается по значению — как это делают алгоритмы стандартной
    // библиотеки. Именно поэтому лямбда с захватом по значению и mutable не
    // выносит изменения наружу: меняется копия.
    template <class F>
    void for_each_room(F fn) const {
        for (const auto& [name, room] : rooms_) {
            fn(name, room.history);
        }
    }

private:
    [[nodiscard]] std::shared_ptr<User> require_user(std::string_view nick) const {
        std::shared_ptr<User> user = find_user(nick);
        if (!user) {
            throw UnknownUserError(std::string(nick));
        }
        return user;
    }

    static std::size_t purge(Members& members) {
        std::size_t removed = 0;
        for (auto it = members.begin(); it != members.end();) {
            if (it->expired()) {
                it = members.erase(it);
                ++removed;
            } else {
                ++it;
            }
        }
        return removed;
    }

    static void reserve_one_more(History& history) {
        if (history.size() == history.capacity()) {
            // Удвоение, а не +1: иначе каждая рассылка перевыделяла бы
            // историю, и добавление стало бы линейным вместо амортизированно
            // постоянного.
            history.reserve(history.empty() ? 8 : history.size() * 2);
        }
    }

    // Фиксация рассылки. noexcept здесь — контракт: ёмкость запасена,
    // перемещение не бросает, значит не бросает и push_back. Если контракт
    // всё же нарушен, программа завершится через std::terminate. Это лучше,
    // чем чат, в котором часть участников видит сообщение, а часть нет.
    static void commit(const std::vector<History*>& targets, std::vector<M>& copies) noexcept {
        for (std::size_t i = 0; i < targets.size(); ++i) {
            targets[i]->push_back(std::move(copies[i]));
        }
    }

    // std::less<> включает разнородный поиск: методы принимают string_view
    // и не создают временную строку на каждый вызов.
    using Map = std::map<std::string, Room, std::less<>>;
    Map rooms_;
    std::map<std::string, std::shared_ptr<User>, std::less<>> users_;
    std::uint64_t seq_ = 0;   // последний выданный номер сообщения
};

using ChatStore = BasicChatStore<MessagePtr>;
using History = ChatStore::History;

}  // namespace chat
