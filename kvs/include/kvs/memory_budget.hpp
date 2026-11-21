#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "kvs/errors.hpp"

namespace kvs {

// Этап 9: учёт объёма — отдельная обязанность. До этапа 9 он был размазан
// по BasicStore: поле used_, поле memory_limit_, проверка require_room и
// арифметика в set, modify, del и account (SRP, docs/audit.md, запись 1).
// У него своя причина меняться — на этапе 11 к пределу добавится политика
// вытеснения, — и теперь она меняет этот класс, а не хранилище.
//
// Объём приближённый с этапа 3: длина ключа плюс размер значения.
class MemoryBudget {
public:
    MemoryBudget() = default;
    explicit MemoryBudget(std::optional<std::size_t> limit) noexcept : limit_(limit) {}

    // Поместится ли needed байт; нет — OutOfMemoryError с ключом, на котором
    // отказали. Ничего не меняет: проверка — до изменения хранилища.
    void require(const std::string& key, std::size_t needed) const {
        if (limit_ && needed > *limit_) {
            throw OutOfMemoryError(key, needed, *limit_);
        }
    }

    // Новый объём после изменения, которое уже прошло проверку require.
    void commit(std::size_t used) noexcept { used_ = used; }

    [[nodiscard]] std::size_t used() const noexcept { return used_; }
    [[nodiscard]] std::optional<std::size_t> limit() const noexcept { return limit_; }

    // Объём после замены записи размером removed на запись размером added.
    [[nodiscard]] std::size_t after(std::size_t removed, std::size_t added) const noexcept {
        return used_ - removed + added;
    }

private:
    std::size_t used_ = 0;
    std::optional<std::size_t> limit_;
};

}  // namespace kvs
