#include "chat/message.hpp"

#include <sstream>
#include <type_traits>

#include "chat/overloaded.hpp"

namespace chat {

static_assert(std::is_nothrow_move_constructible_v<Message>,
              "Message must be nothrow move constructible");
static_assert(std::is_copy_constructible_v<Message>, "Message must be copy constructible");
static_assert(std::is_nothrow_destructible_v<Message>,
              "Message must be nothrow destructible");

std::string_view Message::type_name() const noexcept {
    return std::visit(overloaded{
                          [](const TextBody&) -> std::string_view { return "text"; },
                          [](const SystemNotice&) -> std::string_view { return "notice"; },
                          [](const Reaction&) -> std::string_view { return "reaction"; },
                      },
                      body_);
}

std::string Message::to_string() const {
    // std::format появится вместе с C++23 на этапе 5, пока форматируем потоком.
    return std::visit(overloaded{
                          [](const TextBody& body) { return body.text; },
                          [](const SystemNotice& body) { return "* " + body.text; },
                          [](const Reaction& body) {
                              std::ostringstream out;
                              out << ':' << body.emoji << ": x" << body.count;
                              return out.str();
                          },
                      },
                      body_);
}

std::size_t Message::size() const noexcept {
    return std::visit(overloaded{
                          [](const TextBody& body) { return body.text.size(); },
                          [](const SystemNotice& body) { return body.text.size(); },
                          [](const Reaction& body) { return body.emoji.size(); },
                      },
                      body_);
}

}  // namespace chat
