#include "chat/reply.hpp"

#include <algorithm>
#include <exception>
#include <format>
#include <iterator>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "chat/errors.hpp"

namespace chat {
namespace {

// Ответ по исходной причине ошибки. Порядок веток важен: от частного к
// общему. Если поставить ChatError первым, до ParseError очередь не дойдёт
// никогда.
//
// Все ветки ловят по константной ссылке. По значению нельзя: объект
// обрезался бы до базового класса, и поля потомка потерялись бы.
[[nodiscard]] std::string reason_of(const std::exception_ptr& cause) {
    try {
        std::rethrow_exception(cause);
    } catch (const ParseError& e) {
        return e.reason();
    } catch (const UnknownRoomError& e) {
        return "no such room: " + e.room();
    } catch (const NotMemberError& e) {
        return "you are not in " + e.room() + " (use /join)";
    } catch (const NicknameTakenError& e) {
        return "nickname in use: " + e.nick();
    } catch (const UnknownUserError& e) {
        return "no such user: " + e.nick();
    } catch (const MessageNotFoundError& e) {
        return "no message " + std::to_string(e.index()) + " in " + e.room();
    } catch (const TypeMismatchError& e) {
        return "wrong type: expected " + e.expected() + ", got " + e.actual();
    } catch (const CounterOverflowError&) {
        return "reaction counter would overflow";
    } catch (const ChatError& e) {
        return e.what();
    } catch (const std::bad_alloc&) {
        return "out of memory (allocation failed)";
    } catch (const std::exception& e) {
        return std::string("internal: ") + e.what();
    } catch (...) {
        return "internal: unknown error";
    }
}

}  // namespace

std::string Reply::message_line(const ChatStore::Entry& entry, bool with_time) {
    const IMessage& item = *entry.message.get();
    if (with_time) {
        return std::format("{} @{}: <{}> {}", entry.index, item.id().timestamp, item.author(), item.to_string());
    }
    return std::format("{}: <{}> {}", entry.index, item.author(), item.to_string());
}

Response Reply::history(const std::vector<ChatStore::Entry>& entries, bool with_time) {
    std::vector<std::string> lines;
    lines.reserve(entries.size());
    std::ranges::transform(entries, std::back_inserter(lines),
                           [with_time](const ChatStore::Entry& entry) { return message_line(entry, with_time); });
    return Reply::lines(lines);
}

Response Reply::error(const std::exception_ptr& error) {
    // Свёртка: каждое звено контекста дописывается к ответу в скобках.
    std::string text = std::ranges::fold_left(context_of(error), "ERR " + reason_of(root_cause(error)),
                                              [](std::string acc, const std::string& step) {
                                                  return std::move(acc) + " (" + step + ")";
                                              });
    return Response{false, std::move(text)};
}

}  // namespace chat
