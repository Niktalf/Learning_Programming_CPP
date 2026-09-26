#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "message.hpp"


template <class M>
class BasicChatStore {
public:
    using History = std::vector<M>;

    bool create_room(std::string room) {
        if (rooms_.find(room) != rooms_.end()) {
            return false;
        }
        rooms_.insert(std::make_pair(std::move(room), History{}));
        return true;
    }
    void post(std::string room, M message) {
        typename Map::iterator it = rooms_.find(room);
        if (it == rooms_.end()) {
            it = rooms_.insert(std::make_pair(std::move(room), History{})).first;
        }
        it->second.push_back(std::move(message));
    }

    const History* history(const std::string& room) const noexcept {
        typename Map::const_iterator it = rooms_.find(room);
        return it == rooms_.end() ? nullptr : &it->second;
    }

    bool drop_room(const std::string& room) {
        typename Map::iterator it = rooms_.find(room);
        if (it == rooms_.end()) {
            return false;
        }
        rooms_.erase(it);
        return true;
    }

    std::size_t room_count() const noexcept { return rooms_.size(); }
    bool empty() const noexcept { return rooms_.empty(); }

    template <class F>
    void for_each_room(F fn) const {
        for (const auto& entry : rooms_) {
            fn(entry.first, entry.second);
        }
    }

private:
    using Map = std::map<std::string, History>;
    Map rooms_;
};

using ChatStore = BasicChatStore<Message>;
