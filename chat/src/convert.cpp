#include "chat/convert.hpp"

#include <algorithm>
#include <string>
#include <string_view>

namespace chat {

RoomList split_rooms(std::string_view list) {
    RoomList result;
    std::size_t from = 0;
    while (true) {
        const std::size_t comma = list.find(',', from);
        const std::string_view room =
            list.substr(from, comma == std::string_view::npos ? std::string_view::npos : comma - from);
        if (const auto problem = room_name_problem(room); problem) {
            throw ParseError(*problem, std::string(list));
        }
        // Одна комната дважды — ошибка ввода, а не повод отправить дважды.
        if (std::ranges::find(result.rooms, room) != result.rooms.end()) {
            throw ParseError("room listed twice: " + std::string(room), std::string(list));
        }
        result.rooms.push_back(room);
        if (comma == std::string_view::npos) {
            return result;
        }
        from = comma + 1;
    }
}

}  // namespace chat
