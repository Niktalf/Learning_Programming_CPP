#include "handler.hpp"

#include <exception>
#include <new>
#include <utility>

#include "errors.hpp"
#include "parser.hpp"

Response handle(ChatStore& store, std::string line) {
    const InputLine input{std::move(line)};
    try {
        return execute(store, parse(input));
    } catch (const ParseError& e) {
        return Response {
            .ok = false,
            .text = "ERR " + e.reason()
        };
    } catch (const UnknownRoomError& e) {
        return Response {
            .ok = false,
            .text = "ERR no such room: " + e.room()
        };
    } catch (const NicknameTakenError& e) {
        return Response {
            .ok = false,
            .text = "ERR nickname in use: " + e.nick()
        };
    } catch (const UnknownUserError& e) {
        return Response {
            .ok = false,
            .text = "ERR no such user: " + e.nick()
        };
    } catch (const TypeMismatchError& e) {
        return Response {
            .ok = false,
            .text = "ERR wrong type: expected " + e.expected() + ", got " + e.actual()
        };
    } catch (const ChatError& e) {
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
