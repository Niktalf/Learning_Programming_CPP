#include "executor.hpp"

#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "errors.hpp"
#include "overloaded.hpp"

namespace {
    std::string describe(const Task& task) {
        std::ostringstream out;
        out << "id=" << task.id() << " priority=" << task.priority() << " state=" << name(task.state());
        if (const auto worker = task.holder()) {
            out << " у=" << worker->name();
        } else if (task.abandoned()) {
            out << " (abandoned: the worker is missing)";
        }
        out << ' ' << task.to_string();
        return out.str();
    }
}  // namespace

Response Service::execute(const Command& command) {
    return std::visit(overloaded{
        [&](const CreateCommand& c) {
            return store_.create_queue(std::string(c.queue)) ?
            Response {
                .ok = true,
                .text = "OK"
            } :
            Response {
                .ok = true,
                .text = "(there is already a queue)"
            };
        },
        [&](const SubmitCommand& c) {
            const TaskId id = next_id_++;
            store_.submit(c.queue, Task(id, std::string(c.body), c.priority));
            return Response {
                .ok = true,
                .text = "OK id=" + std::to_string(id)
            };
        },
        [&](const BatchCommand& c) {
            std::vector<std::pair<std::string, Task>> items;
            items.reserve(c.queues.size());
            for (const auto& queue : c.queues) {
                items.emplace_back(std::string(queue), Task(next_id_++, std::string(c.body), c.priority));
            }
            store_.submit_batch(items);
            return Response {
                .ok = true,
                .text = "OK " + std::to_string(items.size()) + " tasks"
            };
        },
        [&](const PollCommand& c) {
            if (const auto task = store_.lease(c.queue, c.worker); task) {
                return Response{
                    .ok = true,
                    .text = describe(*task)
                };
            }
            return Response {
                .ok = true,
                .text = "(queue is empty)"
            };
        },
        [&](const AckCommand& c) {
            store_.transition(c.id, State::Done);
            return Response {
                .ok = true,
                .text = "OK"
            };
        },
        [&](const FailCommand& c) {
            store_.transition(c.id, State::Failed);
            return Response {
                .ok = true,
                .text = "OK"
            };
        },
        [&](const ReleaseCommand& c) {
            store_.transition(c.id, State::Pending);
            return Response {
                .ok = true,
                .text = "OK"
            };
        },
        [&](const StatusCommand& c) {
            return Response {
                .ok = true,
                .text = describe(store_.find(c.id))
            };
        },
        [&](const QueuesCommand&) {
            std::ostringstream out;
            store_.for_each_queue([&out](const std::string& name_, const Queue& tasks) {
                out << name_ << "  (" << tasks.size() << ")\n";
            });
            const std::string text = out.str();
            return Response {
                .ok = true,
                .text = text.empty() ? "(there are no queues)\n" : text
            };
        },
        [&](const TypeCommand& c) {
            return Response {
                .ok = true,
                .text = std::string(store_.find(c.id).type_name())
            };
        },
        [&](const BumpCommand& c) {
            Task& task = store_.find(c.id);
            auto* number = task.as<NumericPayload>();
            if (number == nullptr) {
                throw TypeMismatchError(c.id, "number", std::string(task.type_name()));
            }
            number->add(c.by);
            return Response {
                .ok = true,
                .text = std::to_string(number->value())
            };
        },
        [&](const WorkerCommand& c) {
            return store_.register_worker(std::string(c.name)) ?
            Response {
                .ok = true,
                .text = "OK"
            } :
            Response {
                .ok = true,
                .text = "(there is already a worker)"
            };
        },
        [&](const GoneCommand& c) {
            if (!store_.remove_worker(c.name)) {
                throw UnknownWorkerError(std::string(c.name));
            }
            return Response  {
                .ok = true,
                .text = "OK"
            };
        },
    }, command);
}
