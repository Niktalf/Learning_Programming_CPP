#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "chat/message_id.hpp"

namespace chat {

// Этап 9: заголовок сообщения — кто написал и идентификатор (номер и
// отметка времени). Общий для всех видов сообщений.
struct MessageHeader {
    std::string author;
    MessageId id;
};

// Этап 4: сообщение — иерархия классов вместо std::variant, и комната хранит
// std::vector<std::unique_ptr<IMessage>>.
//
// Деструктор виртуальный, и это обязательно. Сообщения удаляются через
// std::unique_ptr<IMessage>, то есть через указатель на базовый класс. Без
// virtual это неопределённое поведение: вызовется только ~IMessage, а
// деструктор наследника — с текстом сообщения — не выполнится. unique_ptr
// не спасает: он вызывает delete по статическому типу.
//
// Этап 9: композиция вместо наследования данных. До этапа 9 автор, время и
// идентификатор жили полями в базе, «чтобы не повторять их в трёх
// наследниках», — переиспользование кода через наследование, а не
// отношение «является» (docs/audit.md, запись 9). Заодно база хранила время
// дважды: поле at_, которое никто не заполнял, и отметку в идентификаторе.
// Теперь IMessage — интерфейс без данных, а каждое сообщение *имеет*
// заголовок MessageHeader. Доступ к автору и идентификатору — обычные
// функции интерфейса поверх header(): шаблонный метод (NVI), код у них один.
class IMessage {
public:
    virtual ~IMessage() = default;

    [[nodiscard]] const std::string& author() const noexcept { return header().author; }

    // Этап 5: идентификатор, который выдаёт хранилище при отправке. Копии
    // сообщения, разосланные в несколько комнат, делаются clone и несут тот
    // же идентификатор: это одно сообщение.
    [[nodiscard]] const MessageId& id() const noexcept { return header().id; }
    void assign_id(MessageId id) noexcept { header().id = std::move(id); }

    [[nodiscard]] virtual const MessageHeader& header() const noexcept = 0;
    [[nodiscard]] virtual std::string_view type_name() const noexcept = 0;
    [[nodiscard]] virtual std::string to_string() const = 0;
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;

    // Виртуальный конструктор копирования. Копировать сообщение через
    // указатель на базу нельзя: неизвестно, какой класс создавать. На clone
    // держится сильная гарантия рассылки из этапа 3: копии сообщения для
    // каждой комнаты делаются им.
    [[nodiscard]] virtual std::unique_ptr<IMessage> clone() const = 0;

protected:
    IMessage() = default;
    // Копирование базы — только для наследников, иначе объект обрезался бы.
    IMessage(const IMessage&) = default;
    IMessage& operator=(const IMessage&) = default;

    // Изменяемый заголовок — только для assign_id.
    [[nodiscard]] virtual MessageHeader& header() noexcept = 0;
};

using MessagePtr = std::unique_ptr<IMessage>;

class TextMessage final : public IMessage {
public:
    TextMessage(std::string author, std::string text) noexcept
        : header_{std::move(author), {}}, text_(std::move(text)) {}

    [[nodiscard]] const MessageHeader& header() const noexcept override { return header_; }
    [[nodiscard]] std::string_view type_name() const noexcept override { return "text"; }
    [[nodiscard]] std::string to_string() const override { return text_; }
    [[nodiscard]] std::size_t size() const noexcept override { return text_.size(); }
    [[nodiscard]] MessagePtr clone() const override;

    [[nodiscard]] const std::string& text() const noexcept { return text_; }

private:
    [[nodiscard]] MessageHeader& header() noexcept override { return header_; }

    MessageHeader header_;
    std::string text_;
};

// Системное уведомление: вошёл, вышел, сменил тему.
class SystemMessage final : public IMessage {
public:
    SystemMessage(std::string author, std::string text) noexcept
        : header_{std::move(author), {}}, text_(std::move(text)) {}

    [[nodiscard]] const MessageHeader& header() const noexcept override { return header_; }
    [[nodiscard]] std::string_view type_name() const noexcept override { return "notice"; }
    [[nodiscard]] std::string to_string() const override { return "* " + text_; }
    [[nodiscard]] std::size_t size() const noexcept override { return text_.size(); }
    [[nodiscard]] MessagePtr clone() const override;

private:
    [[nodiscard]] MessageHeader& header() noexcept override { return header_; }

    MessageHeader header_;
    std::string text_;
};

// Реакция :name: со счётчиком. Единственное изменяемое сообщение.
class ReactionMessage final : public IMessage {
public:
    ReactionMessage(std::string author, std::string emoji, std::int64_t count = 1) noexcept
        : header_{std::move(author), {}}, emoji_(std::move(emoji)), count_(count) {}

    [[nodiscard]] const MessageHeader& header() const noexcept override { return header_; }
    [[nodiscard]] std::string_view type_name() const noexcept override { return "reaction"; }
    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::size_t size() const noexcept override { return emoji_.size(); }
    [[nodiscard]] MessagePtr clone() const override;

    [[nodiscard]] const std::string& emoji() const noexcept { return emoji_; }
    [[nodiscard]] std::int64_t count() const noexcept { return count_; }
    void set_count(std::int64_t count) noexcept { count_ = count; }

private:
    [[nodiscard]] MessageHeader& header() noexcept override { return header_; }

    MessageHeader header_;
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

// Этап 8: отметка времени для индекса истории по времени — из идентификатора
// сообщения, в миллисекундах от эпохи. Сообщение без идентификатора — 0.
[[nodiscard]] inline std::uint64_t timestamp_of(const MessagePtr& message) noexcept {
    return message->id().timestamp;
}

}  // namespace chat
