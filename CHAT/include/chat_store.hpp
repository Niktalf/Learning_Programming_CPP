#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string_view>
#include <string>
#include <utility>
#include <vector>

#include "message.hpp"

template <class M>
class BasicChatStore {
public:
    using History = std::vector<M>;

    bool create_room(std::string room) {
        return rooms_.try_emplace(std::move(room)).second;
    }
    void post(std::string room, M message) {
        rooms_[std::move(room)].push_back(std::move(message));
    }

    [[nodiscard]] std::optional<std::reference_wrapper<const History>>
    history(std::string_view room) const noexcept {
        typename Map::const_iterator it = rooms_.find(room);
        if (it == rooms_.end()) {
            return std::nullopt;
        }
        return std::cref(it->second);
    }

    [[nodiscard]] M* message_at(std::string_view room, std::size_t index) noexcept {
        typename Map::iterator it = rooms_.find(room);
        if (it == rooms_.end() || index >= it->second.size()) {
            return nullptr;
        }
        return &it->second[index];
    }

    [[nodiscard]] bool contains(std::string_view room) const noexcept {
        return rooms_.find(room) != rooms_.end();
    }

    bool drop_room(std::string_view room) {
        typename Map::iterator it = rooms_.find(room);
        if (it == rooms_.end()) {
            return false;
        }
        rooms_.erase(it);
        return true;
    }

    [[nodiscard]] std::size_t room_count() const noexcept { return rooms_.size(); }
    [[nodiscard]] bool empty() const noexcept { return rooms_.empty(); }

    template <class F>
    void for_each_room(F fn) const {
        for (const auto& [room, history] : rooms_) {
            fn(room, history);
        }
    }

private:
    using Map = std::map<std::string, History, std::less<>>;
    Map rooms_;
};

using ChatStore = BasicChatStore<Message>;
using History = ChatStore::History;
