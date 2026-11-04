// Звёздочка этапа 4: цикл из shared_ptr. Утечку здесь видно по счётчику
// живых объектов, без санитайзера. Чтобы сам тест не утекал, цикл в конце
// разрывается руками.

#include <gtest/gtest.h>

#include <memory>

#include "cycle.hpp"

using taskq::cycle::alive;
using taskq::cycle::lease;

TEST(Cycle, SharedHolderKeepsBothAlive) {
    using Worker = taskq::cycle::Worker<std::shared_ptr>;
    using Job = taskq::cycle::Job<std::shared_ptr>;
    std::weak_ptr<Worker> worker_observer;
    std::weak_ptr<Job> job_observer;
    {
        const auto worker = std::make_shared<Worker>("w1");
        const auto job = std::make_shared<Job>("make all");
        lease(worker, job);
        worker_observer = worker;
        job_observer = job;
    }
    // Внешних ссылок нет, а оба живы — держат друг друга. И задача не
    // считается брошенной: её держатель «жив», потому что она сама его держит.
    EXPECT_FALSE(worker_observer.expired());
    EXPECT_FALSE(taskq::cycle::abandoned(*job_observer.lock()));
    EXPECT_EQ(alive(), 2);

    job_observer.lock()->holder.reset();   // разрыв руками
    EXPECT_TRUE(worker_observer.expired());
    EXPECT_TRUE(job_observer.expired());
    EXPECT_EQ(alive(), 0);
}

TEST(Cycle, WeakHolderBreaksTheCycle) {
    using Worker = taskq::cycle::Worker<std::weak_ptr>;
    using Job = taskq::cycle::Job<std::weak_ptr>;
    std::weak_ptr<Job> job_observer;
    {
        const auto worker = std::make_shared<Worker>("w1");
        const auto job = std::make_shared<Job>("make all");
        lease(worker, job);
        job_observer = job;
        EXPECT_EQ(worker.use_count(), 1);   // задача не владеет воркером
    }
    EXPECT_TRUE(job_observer.expired());
    EXPECT_EQ(alive(), 0);
}

TEST(Cycle, JobOutlivingWorkerIsAbandoned) {
    using Worker = taskq::cycle::Worker<std::weak_ptr>;
    using Job = taskq::cycle::Job<std::weak_ptr>;
    const auto job = std::make_shared<Job>("make all");
    {
        const auto worker = std::make_shared<Worker>("w1");
        lease(worker, job);
        EXPECT_FALSE(taskq::cycle::abandoned(*job));
        worker->current.reset();   // воркер исчезает, задачу держим мы
    }
    // Воркер исчез — задача брошена и это видит.
    EXPECT_TRUE(taskq::cycle::abandoned(*job));
    EXPECT_EQ(alive(), 1);
}
