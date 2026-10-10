#include "imessage.hpp"

#include <sstream>
#include <type_traits>

static_assert(std::has_virtual_destructor_v<IMessage>, "deletion via unique_ptr<iMessage> requires a virtual destructor");

std::unique_ptr<IMessage> TextMessage::clone() const { return std::make_unique<TextMessage>(*this); }
std::unique_ptr<IMessage> SystemMessage::clone() const { return std::make_unique<SystemMessage>(*this); }
std::unique_ptr<IMessage> ReactionMessage::clone() const { return std::make_unique<ReactionMessage>(*this); }

bool TextMessage::equals(const IMessage& other) const noexcept {
    const auto* o = dynamic_cast<const TextMessage*>(&other);
    return o != nullptr && o->text_ == text_;
}

bool SystemMessage::equals(const IMessage& other) const noexcept {
    const auto* o = dynamic_cast<const SystemMessage*>(&other);
    return o != nullptr && o->text_ == text_;
}

bool ReactionMessage::equals(const IMessage& other) const noexcept {
    const auto* o = dynamic_cast<const ReactionMessage*>(&other);
    return o != nullptr && o->emoji_ == emoji_ && o->count_ == count_;
}

std::string ReactionMessage::to_string() const {
    std::ostringstream out;
    out << emoji_ << " x" << count_;
    return out.str();
}
