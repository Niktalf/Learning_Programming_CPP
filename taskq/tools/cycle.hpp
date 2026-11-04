#pragma once

#include <memory>
#include <string>
#include <utility>

// Звёздочка этапа 4: цикл из shared_ptr и его разрыв.
//
// Воркер держит задачу, которую выполняет, — без неё ему нечего делать.
// Задача держит воркера — держателя аренды, чтобы знать, кто её взял. Если
// обе ссылки владеющие, воркер держит задачу, задача держит воркера, и
// счётчики никогда не дойдут до нуля: оба объекта переживут последнюю внешнюю
// ссылку и не освободятся до конца программы. Санитайзер покажет утечку.
//
// Вид ссылки от задачи к воркеру — параметр шаблона: std::shared_ptr или
// std::weak_ptr. Настоящий сервис устроен как второй вариант: аренда хранит
// weak_ptr на воркера, владеет воркерами реестр.
//
// Правило: обратная ссылка почти всегда должна быть weak. Здесь «обратная» —
// от задачи к воркеру: воркер может исчезнуть, и задача не должна его
// удерживать, — наоборот, должна это заметить.
namespace taskq::cycle {

// Сколько объектов обоих видов сейчас живо.
inline int& alive() {
    static int count = 0;
    return count;
}

template <template <class> class Link>
struct Worker;

template <template <class> class Link>
struct Job {
    explicit Job(std::string job_body) : body(std::move(job_body)) { ++alive(); }
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;
    ~Job() { --alive(); }

    std::string body;
    Link<Worker<Link>> holder;   // shared_ptr или weak_ptr
};

template <template <class> class Link>
struct Worker {
    explicit Worker(std::string worker_name) : name(std::move(worker_name)) { ++alive(); }
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;
    ~Worker() { --alive(); }

    std::string name;
    std::shared_ptr<Job<Link>> current;   // владеющая ссылка
};

template <template <class> class Link>
void lease(const std::shared_ptr<Worker<Link>>& worker, const std::shared_ptr<Job<Link>>& job) {
    worker->current = job;
    job->holder = worker;
}

// Брошена ли задача. С weak_ptr это видно по истёкшему держателю.
inline bool abandoned(const Job<std::weak_ptr>& job) {
    return job.holder.expired();
}

// С shared_ptr задача не может стать брошенной: она сама держит воркера.
inline bool abandoned(const Job<std::shared_ptr>& job) {
    return job.holder == nullptr;
}

}  // namespace taskq::cycle
