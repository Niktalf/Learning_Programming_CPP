#include "executor.hpp"

#include <sstream>
#include <string>
#include <variant>

#include "overloaded.hpp"

Response Service::execute(const Command& command) {
    return std::visit(overloaded{
        [&](const SubmitCommand& c) {
            const TaskId id = next_id_++;
            store_.submit(std::string(c.queue), Task(id, std::string(c.body), c.priority));
            std::ostringstream out;
            out << "OK id=" << id;
            return Response{true, out.str()};
        },
        [&](const PollCommand& c) -> Response {
            if (const auto task = store_.take(c.queue); task) {
                std::ostringstream out;
                out << "id=" << task->id() << ' ' << task->to_string();
                return Response{true, out.str()};
            }
            return Response{false, "(no task: the queue is empty or does not exist)"};
        },
        [&](const AckCommand& c) -> Response {
            if (const auto task = store_.take(c.queue); task) {
                std::ostringstream out;
                out << "confirmed id=" << task->id();
                return Response{true, out.str()};
            }
            return Response{false, "(no task to confirm)"};
        },
        [&](const StatusCommand& c) -> Response {
            if (const auto task = store_.peek(c.queue); task) {
                std::ostringstream out;
                out << "id=" << task->id() << " priority=" << task->priority()
                << ' ' << task->to_string();
                return Response{true, out.str()};
            }
            return Response{false, "(no task: the queue is empty or does not exist)"};
        },
        [&](const QueuesCommand&) {
            std::ostringstream out;
            store_.for_each_queue([&out](const std::string& name, const Queue& tasks) {
                out << name << "  (" << tasks.size() << ")\n";
            });
            const std::string text = out.str();
            return Response{true, text.empty() ? "(no queues)\n" : text};
        },
        [&](const TypeCommand& c) -> Response {
            if (const auto task = store_.peek(c.queue); task) {
                return Response{true, std::string(task->type_name())};
            }
            return Response{false, "(no task)"};
        },
        [&](const BumpCommand& c) -> Response {
            Task* task = store_.front(c.queue);
            if (task == nullptr) {
                return Response{false, "(no task)"};
            }
            auto* number = task->get_if<NumericPayload>();
            if (number == nullptr) {
                return Response{false, "(type doesn't support BUMP: " + std::string(task->type_name()) + ")"};
            }
            number->value += c.by;
            std::ostringstream out;
            out << number->value;
            return Response{true, out.str()};
        },
    }, command);
}
