#pragma once

#include <string>
#include <utility>

namespace taskq {

// Воркер — тот, кто берёт задачи. Этап 4: объект с идентичностью. Им
// владеет реестр воркеров в Service (shared_ptr), выданная задача на него
// только смотрит (weak_ptr). Исчез воркер — задача видит, что держатель
// аренды пропал, и её можно вернуть в очередь, не дожидаясь срока аренды.
// На этапе 15 воркер исчезнет вместе с сетевым соединением.
class Worker {
public:
    explicit Worker(std::string name) noexcept : name_(std::move(name)) {}

    // Две копии одного воркера были бы двумя разными исполнителями с одним
    // именем. Копирование запрещено.
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    [[nodiscard]] const std::string& name() const noexcept { return name_; }

private:
    std::string name_;
};

}  // namespace taskq
