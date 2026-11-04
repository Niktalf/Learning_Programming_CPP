// Звёздочка этапа 4: цикл из shared_ptr. Утечку здесь видно по счётчику
// живых объектов, без санитайзера. Чтобы сам тест не утекал — сборка под
// санитайзером иначе упала бы на выходе, — цикл в конце разрывается руками.

#include <gtest/gtest.h>

#include <memory>

#include "cycle.hpp"

using kvs::cycle::alive;
using kvs::cycle::Client;
using kvs::cycle::subscribe;

TEST(Cycle, SharedBackReferenceKeepsBothAlive) {
    std::weak_ptr<Client<std::shared_ptr>> observer;
    {
        const auto client = std::make_shared<Client<std::shared_ptr>>("alice");
        subscribe(client, "counter");
        observer = client;
        // Две ссылки: внешняя и обратная из подписки.
        EXPECT_EQ(client.use_count(), 2);
    }
    // Внешней ссылки нет, а клиент жив: его держит собственная подписка.
    EXPECT_FALSE(observer.expired());
    EXPECT_EQ(alive(), 2);

    // Разрыв руками: подписка отпускает клиента, и цепочка рушится целиком.
    observer.lock()->subscriptions.front()->owner.reset();
    EXPECT_TRUE(observer.expired());
    EXPECT_EQ(alive(), 0);
}

TEST(Cycle, WeakBackReferenceBreaksTheCycle) {
    std::weak_ptr<Client<std::weak_ptr>> observer;
    std::weak_ptr<kvs::cycle::Subscription<std::weak_ptr>> subscription;
    {
        const auto client = std::make_shared<Client<std::weak_ptr>>("alice");
        subscribe(client, "counter");
        observer = client;
        subscription = client->subscriptions.front();
        // weak_ptr не увеличивает счётчик владельцев.
        EXPECT_EQ(client.use_count(), 1);
        EXPECT_EQ(kvs::cycle::owner_name(*subscription.lock()), "alice");
    }
    EXPECT_TRUE(observer.expired());
    EXPECT_TRUE(subscription.expired());
    EXPECT_EQ(alive(), 0);
}

TEST(Cycle, ObserverOutlivingOwnerSeesItGone) {
    // Подписку держат отдельно от клиента — например, в очереди уведомлений,
    // — и клиент уходит раньше неё. weak_ptr не продлевает клиенту жизнь, а
    // lock честно сообщает, что уведомлять некого.
    std::shared_ptr<kvs::cycle::Subscription<std::weak_ptr>> subscription;
    {
        const auto client = std::make_shared<Client<std::weak_ptr>>("alice");
        subscribe(client, "counter");
        subscription = client->subscriptions.front();
        EXPECT_EQ(kvs::cycle::owner_name(*subscription), "alice");
    }
    EXPECT_EQ(kvs::cycle::owner_name(*subscription), "(gone)");
    EXPECT_EQ(alive(), 1);   // клиент освобождён, подписка ещё нет
}
