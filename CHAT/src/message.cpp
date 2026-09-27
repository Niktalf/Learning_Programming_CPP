#include "message.hpp"

#include <sstream>
#include <type_traits>

#include "overloaded.hpp"

static_assert(std::is_nothrow_move_constructible_v<Message>, "Message must be transmitted without exception");
static_assert(std::is_nothrow_move_assignable_v<Message>, "Message must be assigned by movement without exceptions");
static_assert(std::is_copy_constructible_v<Message>, "Message must be copied");
static_assert(std::is_nothrow_destructible_v<Message>, "Message must be destroyed without exception");

std::string_view Message::type_name() const noexcept {
    return std::visit(overloaded{
        [](const TextBody&) -> std::string_view { return "text"; },
        [](const SystemNotice&) -> std::string_view { return "notice"; },
        [](const Reaction&) -> std::string_view { return "reaction"; },
    }, body_);
}

std::string Message::to_string() const {
    return std::visit(overloaded{
        [](const TextBody& b) { return b.text; },
        [](const SystemNotice& b) { return "* " + b.text; },
        [](const Reaction& b) {
            std::ostringstream out;
            out << b.emoji << " x" << b.count;
            return out.str();
        },
    }, body_);
}

std::size_t Message::size() const noexcept {
    return std::visit(overloaded{
        [](const TextBody& b) { return b.text.size(); },
        [](const SystemNotice& b) { return b.text.size(); },
        [](const Reaction& b) { return b.emoji.size(); },
    }, body_);
}
