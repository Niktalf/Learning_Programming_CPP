#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <string>
#include <utility>
#include <vector>

#include "errors.hpp"
#include "message.hpp"
#include "user.hpp"

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

    void post_many(const std::vector<std::string>& rooms, const M& message) {
        Map next = rooms_;
        for (const auto& room : rooms) {
            const auto it = next.find(room);
            if (it == next.end()) {
                throw UnknownRoomError(room);
            }
            it->second.push_back(message);
        }
        rooms_.swap(next);
    }
    void claim_nick(std::string nick) {
        if (users_.find(nick) != users_.end()) {
            throw NicknameTakenError(std::move(nick));
        }
        auto user = std::make_shared<User>(nick);
        users_.emplace(std::move(nick), std::move(user));
    }

    bool release_nick(const std::string_view nick) {
        const auto it = users_.find(nick);
        if (it == users_.end()) {
            return false;
        }
        users_.erase(it);
        return true;
    }

    void invite(std::string_view room, const std::string_view nick) {
        const auto user = users_.find(nick);
        if (user == users_.end()) {
            throw UnknownUserError(std::string(nick));
        }
        if (rooms_.find(room) != rooms_.end()) {
            throw UnknownRoomError(std::string(room));
        }
        members_[std::string(room)].push_back(user->second);
    }

    [[nodiscard]] std::vector<std::shared_ptr<User>> members(std::string_view room) {
        if (rooms_.find(room) != rooms_.end()) {
            throw UnknownRoomError(std::string(room));
        }
        std::vector<std::shared_ptr<User>> alive;
        const auto it = members_.find(room);
        if (it == members_.end()) {
            return alive;
        }
        auto& observers = it->second;
        for (auto w = observers.begin(); w != observers.end();) {
            if (auto user = w->lock()) {
                alive.push_back(std::move(user));
                ++w;
            } else {
                w = observers.erase(w);
            }
        }
        return alive;
    }

    [[nodiscard]] std::size_t user_count() const noexcept { return users_.size(); }

    friend void swap(BasicChatStore& a, BasicChatStore& b) noexcept {
        a.rooms_.swap(b.rooms_);
        a.users_.swap(b.users_);
        a.members_.swap(b.members_);
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
    std::map<std::string, std::shared_ptr<User>, std::less<>> users_;
    std::map<std::string, std::vector<std::weak_ptr<User>>, std::less<>> members_;
};

using ChatStore = BasicChatStore<Message>;
using History = ChatStore::History;
