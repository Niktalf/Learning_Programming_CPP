#include "executor.hpp"

#include <sstream>
#include <string>
#include <variant>

#include "overloaded.hpp"

Response execute(ChatStore& store, const Command& command) {
    return std::visit(
        overloaded{
        [&](const JoinCommand& c) {
            return store.create_room(std::string(c.room)) ? Response{true, "OK"}
                                                          : Response{true, "(room already exists)"};
        },
        [&](const LeaveCommand& c) {
            return store.drop_room(c.room) ? Response{true, "OK"}
                                           : Response{false, "(no such room)"};
        },
        [&](const MsgCommand& c) {
            store.post(std::string(c.room), Message("me", std::string(c.text)));
            return Response{true, "OK"};
        },
        [&](const HistoryCommand& c) -> Response {
            if (const auto history = store.history(c.room); history) {
                const History& messages = history->get();
                const std::size_t from = messages.size() > c.limit ? messages.size() - c.limit : 0;
                std::ostringstream out;
                for (std::size_t i = from; i < messages.size(); ++i) {
                    out << i << ": <" << messages[i].author() << "> " << messages[i].to_string() << '\n';
                }
                const std::string text = out.str();
                return Response{true, text.empty() ? "(empty)\n" : text};
            }
            return Response{false, "(no such room)\n"};
        },
        [&](const RoomsCommand&) {
            std::ostringstream out;
            store.for_each_room([&out](const std::string& room, const History& messages) {
                out << room << "  (" << messages.size() << ")\n";
            });
            const std::string text = out.str();
            return Response{true, text.empty() ? "(no rooms)\n" : text};
        },
        [&](const TypeCommand& c) -> Response {
            if (const Message* message = store.message_at(c.room, c.index)) {
                return Response{true, std::string(message->type_name())};
            }
            return Response{false, "(no such message)"};
        },
        [&](const ReactCommand& c) -> Response {
            Message* message = store.message_at(c.room, c.index);
            if (message == nullptr) {
                return Response{false, "(no such message)"};
            }
            auto* reaction = message->get_if<Reaction>();
            if (reaction == nullptr) {
                return Response{false, "(type doesn't support REACT: " + std::string(message->type_name()) + ")"};
            }
            reaction->count += c.by;
            std::ostringstream out;
            out << reaction->count;
            return Response{true, out.str()};
        },
    }, command);
}
