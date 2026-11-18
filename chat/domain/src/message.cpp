#include "chat/message.hpp"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace chat {

// Удаление через указатель на базу корректно только с виртуальным деструктором.
static_assert(std::has_virtual_destructor_v<IMessage>, "IMessage needs a virtual destructor");
// Рассылка переносит копии в истории перемещением unique_ptr; оно не бросает.
static_assert(std::is_nothrow_move_constructible_v<MessagePtr>,
              "moving a message pointer must not throw");

// clone всех наследников устроен одинаково: копирующий конструктор
// конкретного класса, результат — в unique_ptr на базу. make_unique, а не
// new: ни одного new в рабочем коде.

MessagePtr TextMessage::clone() const {
    return std::make_unique<TextMessage>(*this);
}

MessagePtr SystemMessage::clone() const {
    return std::make_unique<SystemMessage>(*this);
}

std::string ReactionMessage::to_string() const {
    // std::format появится вместе с C++23 на этапе 5.
    return ':' + emoji_ + ": x" + std::to_string(count_);
}

MessagePtr ReactionMessage::clone() const {
    return std::make_unique<ReactionMessage>(*this);
}

MessagePtr make_text(std::string author, std::string text) {
    return std::make_unique<TextMessage>(std::move(author), std::move(text));
}

MessagePtr make_notice(std::string author, std::string text) {
    return std::make_unique<SystemMessage>(std::move(author), std::move(text));
}

MessagePtr make_reaction(std::string author, std::string emoji) {
    return std::make_unique<ReactionMessage>(std::move(author), std::move(emoji));
}

}  // namespace chat
