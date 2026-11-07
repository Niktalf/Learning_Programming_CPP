#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "chat/message_id.hpp"

namespace chat {

using Timestamp = std::chrono::system_clock::time_point;

// Этап 4: сообщение — иерархия классов вместо std::variant, и комната хранит
// std::vector<std::unique_ptr<IMessage>>.
//
// Деструктор виртуальный, и это обязательно. Сообщения удаляются через
// std::unique_ptr<IMessage>, то есть через указатель на базовый класс. Без
// virtual это неопределённое поведение: вызовется только ~IMessage, а
// деструктор наследника — с текстом сообщения — не выполнится. unique_ptr
// не спасает: он вызывает delete по статическому типу.
//
// Общие для всех сообщений поля — автор и время — живут в базе. Это не
// интерфейс в строгом смысле, а абстрактная база с данными; так проще, чем
// повторять одни и те же поля в трёх наследниках.
class IMessage {
public:
    virtual ~IMessage() = default;

    [[nodiscard]] const std::string& author() const noexcept { return author_; }
    [[nodiscard]] Timestamp at() const noexcept { return at_; }

    // Этап 5: идентификатор, который выдаёт хранилище при отправке. Копии
    // сообщения, разосланные в несколько комнат, делаются clone и несут тот
    // же идентификатор: это одно сообщение.
    [[nodiscard]] const MessageId& id() const noexcept { return id_; }
    void assign_id(MessageId id) noexcept { id_ = std::move(id); }

    [[nodiscard]] virtual std::string_view type_name() const noexcept = 0;
    [[nodiscard]] virtual std::string to_string() const = 0;
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;

    // Виртуальный конструктор копирования. Копировать сообщение через
    // указатель на базу нельзя: неизвестно, какой класс создавать. На clone
    // держится сильная гарантия рассылки из этапа 3: копии сообщения для
    // каждой комнаты делаются им.
    [[nodiscard]] virtual std::unique_ptr<IMessage> clone() const = 0;

protected:
    IMessage(std::string author, Timestamp at) noexcept : author_(std::move(author)), at_(at) {}
    // Копирование базы — только для наследников, иначе объект обрезался бы.
    IMessage(const IMessage&) = default;
    IMessage& operator=(const IMessage&) = default;

private:
    std::string author_;
    Timestamp at_;
    MessageId id_;
};

using MessagePtr = std::unique_ptr<IMessage>;

class TextMessage final : public IMessage {
public:
    TextMessage(std::string author, std::string text, Timestamp at = Timestamp{}) noexcept
        : IMessage(std::move(author), at), text_(std::move(text)) {}

    [[nodiscard]] std::string_view type_name() const noexcept override { return "text"; }
    [[nodiscard]] std::string to_string() const override { return text_; }
    [[nodiscard]] std::size_t size() const noexcept override { return text_.size(); }
    [[nodiscard]] MessagePtr clone() const override;

    [[nodiscard]] const std::string& text() const noexcept { return text_; }

private:
    std::string text_;
};

// Системное уведомление: вошёл, вышел, сменил тему.
class SystemMessage final : public IMessage {
public:
    SystemMessage(std::string author, std::string text, Timestamp at = Timestamp{}) noexcept
        : IMessage(std::move(author), at), text_(std::move(text)) {}

    [[nodiscard]] std::string_view type_name() const noexcept override { return "notice"; }
    [[nodiscard]] std::string to_string() const override { return "* " + text_; }
    [[nodiscard]] std::size_t size() const noexcept override { return text_.size(); }
    [[nodiscard]] MessagePtr clone() const override;

private:
    std::string text_;
};

// Реакция :name: со счётчиком. Единственное изменяемое сообщение.
class ReactionMessage final : public IMessage {
public:
    ReactionMessage(std::string author, std::string emoji, std::int64_t count = 1,
                    Timestamp at = Timestamp{}) noexcept
        : IMessage(std::move(author), at), emoji_(std::move(emoji)), count_(count) {}

    [[nodiscard]] std::string_view type_name() const noexcept override { return "reaction"; }
    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::size_t size() const noexcept override { return emoji_.size(); }
    [[nodiscard]] MessagePtr clone() const override;

    [[nodiscard]] const std::string& emoji() const noexcept { return emoji_; }
    [[nodiscard]] std::int64_t count() const noexcept { return count_; }
    void set_count(std::int64_t count) noexcept { count_ = count; }

private:
    std::string emoji_;
    std::int64_t count_;
};

// Фабрики — чтобы вызывающий код не писал make_unique с полным именем класса
// и сразу получал указатель на базу.
[[nodiscard]] MessagePtr make_text(std::string author, std::string text);
[[nodiscard]] MessagePtr make_notice(std::string author, std::string text);
[[nodiscard]] MessagePtr make_reaction(std::string author, std::string emoji);

// Копия элемента истории для рассылки. Хранилище — шаблон по типу элемента,
// и в тестах туда подставляются копируемые типы; для них копия — обычное
// копирование, для сообщения в unique_ptr — clone. Выбор перегрузкой.
template <class M>
[[nodiscard]] M duplicate(const M& message) {
    return message;
}

[[nodiscard]] inline MessagePtr duplicate(const MessagePtr& message) {
    return message->clone();
}

}  // namespace chat
