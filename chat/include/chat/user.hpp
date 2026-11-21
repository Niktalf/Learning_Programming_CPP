#pragma once

#include <chrono>
#include <string>
#include <utility>

namespace chat {

// Пользователь чата. Этап 4: объект, а не строка. Им владеет реестр
// пользователей в хранилище (shared_ptr), комнаты на него только смотрят
// (weak_ptr). На этапе 15 владельцем станет сетевая сессия: отключился
// клиент — объект исчез, и ни одна комната не держит его по ошибке.
class User {
public:
    explicit User(std::string nick) noexcept : nick_(std::move(nick)) {}

    // Пользователь — сущность с идентичностью: две копии одного пользователя
    // были бы двумя разными людьми с одним ником. Копирование запрещено.
    User(const User&) = delete;
    User& operator=(const User&) = delete;

    [[nodiscard]] const std::string& nick() const noexcept { return nick_; }

    // Меняет только хранилище — при смене ника, вместе с ключом реестра.
    void rename(std::string nick) noexcept { nick_ = std::move(nick); }

    // Этап 9: когда пользователь в последний раз что-то делал — по
    // монотонным часам хранилища (IClock::now()). Отмечает хранилище.
    [[nodiscard]] std::chrono::steady_clock::time_point last_active() const noexcept { return last_active_; }
    void touch(std::chrono::steady_clock::time_point at) noexcept { last_active_ = at; }

private:
    std::string nick_;
    std::chrono::steady_clock::time_point last_active_{};
};

}  // namespace chat
