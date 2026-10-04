#include "executor.hpp"

#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include "errors.hpp"
#include "overloaded.hpp"

namespace {
    const History& require_history(const ChatStore& store, const std::string_view room) {
        const auto history = store.history(room);
        if (!history) {
            throw UnknownRoomError(std::string(room));
        }
        return history->get();
    }

    Message& require_message(ChatStore& store, const std::string_view room, const std::size_t index) {
        if (!store.contains(room)) {
            throw UnknownRoomError(std::string(room));
        }
        Message* message = store.message_at(room, index);
        if (message == nullptr) {
            std::ostringstream reason;
            reason << "no message #" << index << " in " << room;
            throw ChatError(reason.str());
        }
        return *message;
    }
}  // namespace

Response execute(ChatStore& store, const Command& command) {
    return std::visit(overloaded{
        [&](const JoinCommand& c) {
            return store.create_room(std::string(c.room)) ?
            Response{
                .ok = true,
                .text = "OK"
            } : Response {
                .ok = true,
                .text = "(room already exists)"
            };
        },
        [&](const LeaveCommand& c) {
            if (!store.drop_room(c.room)) {
                throw UnknownRoomError(std::string(c.room));
            }
            return Response {
                .ok = true,
                .text = "OK"
            };
        },
        [&](const MsgCommand& c) {
            const std::vector<std::string> rooms(c.rooms.begin(), c.rooms.end());
            store.post_many(rooms, Message("me", std::string(c.text)));
            return Response {
                .ok = true,
                .text = "OK"
            };
        },
        [&](const HistoryCommand& c) {
            const History& messages = require_history(store, c.room);
            const std::size_t from = messages.size() > c.limit ? messages.size() - c.limit : 0;
            std::ostringstream out;
            for (std::size_t i = from; i < messages.size(); ++i) {
                out << i << ": <" << messages[i].author() << "> " << messages[i].to_string() << '\n';
            }
            const std::string text = out.str();
            return Response {
                .ok = true,
                .text = text.empty() ? "(empty)\n" : text
            };
        },
        [&](const RoomsCommand&) {
            std::ostringstream out;
            store.for_each_room([&out](const std::string& room, const History& messages) {
                out << room << "  (" << messages.size() << ")\n";
            });
            const std::string text = out.str();
            return Response {
                .ok = true,
                .text = text.empty() ? "(there are no rooms)\n" : text
            };
        },
        [&](const TypeCommand& c) {
            return Response {
                .ok = true,
                .text = std::string(require_message(store, c.room, c.index).type_name())
            };
        },
        [&](const ReactCommand& c) {
            Message& message = require_message(store, c.room, c.index);
            auto* reaction = message.get_if<Reaction>();
            if (reaction == nullptr) {
                throw TypeMismatchError(std::string(c.room), "reaction",
                    std::string(message.type_name()));
            }
            reaction->count += c.by;
            std::ostringstream out;
            out << reaction->count;
            return Response {
                .ok = true,
                .text = out.str()
            };
        },
        [&](const NickCommand& c) {
            store.claim_nick(std::string(c.nick));
            return Response {
                .ok = true,
                .text = "OK"
            };
        },
    }, command);
}
