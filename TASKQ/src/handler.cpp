#include "handler.hpp"

#include <exception>
#include <new>
#include <string>
#include <utility>

#include "errors.hpp"
#include "parser.hpp"

Response handle(Service& service, std::string line) {
    const InputLine input{std::move(line)};
    try {
        return service.execute(parse(input));
    } catch (const ParseError& e) {
        return Response {
            .ok = false,
            .text = "ERR " + e.reason()
        };
    } catch (const UnknownQueueError& e) {
        return Response {
            .ok = false,
            .text = "ERR no such queue: " + e.queue()
        };
    } catch (const UnknownTaskError& e) {
        return Response {
            .ok = false,
            .text = "ERR no such task: " + std::to_string(e.id())
        };
    } catch (const InvalidTransitionError& e) {
        return Response {
            .ok = false,
            .text = "ERR task " + std::to_string(e.id()) + " is " + std::string(name(e.from())) +
                ", cannot become " + std::string(name(e.to()))};
    } catch (const TypeMismatchError& e) {
        return Response {
            .ok = false,
            .text = "ERR wrong type: expected " + e.expected() + ", got " + e.actual()};
    } catch (const TaskQError& e) {
        return Response {
            .ok = false,
            .text = std::string("ERR ") + e.what()
        };
    } catch (const std::bad_alloc&) {
        return Response {
            .ok = false,
            .text = "ERR out of memory"
        };
    } catch (const std::exception& e) {
        return Response {
            .ok = false,
            .text = std::string("ERR internal: ") + e.what()
        };
    } catch (...) {
        return Response {
            .ok = false,
            .text = "ERR internal: unknown error"
        };
    }
}
