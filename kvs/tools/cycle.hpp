#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

// Звёздочка этапа 4: цикл из shared_ptr и его разрыв.
//
// Клиент подписывается на изменения ключа. Клиент владеет своими подписками —
// когда клиент уходит, подписки ему не нужны. Подписка хранит обратную
// ссылку на клиента — чтобы знать, кого уведомить. Это классическая пара
// «наблюдаемый и наблюдатель», и в ней легко замкнуть цикл.
//
// Вид обратной ссылки — параметр шаблона: std::shared_ptr или std::weak_ptr.
// С shared_ptr клиент держит подписку, подписка держит клиента, и счётчики
// никогда не дойдут до нуля: оба объекта переживут последнюю внешнюю ссылку
// и не освободятся до конца программы. Санитайзер покажет это как утечку.
// С weak_ptr обратная ссылка не владеет, и цикла нет.
//
// Признак, по которому такой цикл распознаётся в чужом коде: два объекта,
// каждый хранит shared_ptr на другого — родитель и ребёнок, наблюдаемый и
// наблюдатель, сессия и её запросы. Правило: обратная ссылка почти всегда
// должна быть weak.
namespace kvs::cycle {

// Сколько объектов обоих видов сейчас живо. Счётчик в функции, а не
// статическое поле, — чтобы заголовок можно было включать куда угодно.
inline int& alive() {
    static int count = 0;
    return count;
}

template <template <class> class Back>
struct Client;

template <template <class> class Back>
struct Subscription {
    Subscription(std::string watched, const std::shared_ptr<Client<Back>>& client)
        : key(std::move(watched)), owner(client) {
        ++alive();
    }
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    ~Subscription() { --alive(); }

    std::string key;
    Back<Client<Back>> owner;   // обратная ссылка: shared_ptr или weak_ptr
};

template <template <class> class Back>
struct Client {
    explicit Client(std::string client_name) : name(std::move(client_name)) { ++alive(); }
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    ~Client() { --alive(); }

    std::string name;
    std::vector<std::shared_ptr<Subscription<Back>>> subscriptions;   // прямые ссылки
};

template <template <class> class Back>
void subscribe(const std::shared_ptr<Client<Back>>& client, std::string key) {
    client->subscriptions.push_back(
        std::make_shared<Subscription<Back>>(std::move(key), client));
}

// Имя клиента по подписке. С weak_ptr владельца сначала надо захватить через
// lock: клиент мог уже уйти, и тогда уведомлять некого.
inline std::string owner_name(const Subscription<std::weak_ptr>& subscription) {
    if (const auto client = subscription.owner.lock()) {
        return client->name;
    }
    return "(gone)";
}

inline std::string owner_name(const Subscription<std::shared_ptr>& subscription) {
    return subscription.owner->name;
}

}  // namespace kvs::cycle
