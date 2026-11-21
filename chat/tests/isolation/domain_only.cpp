// Этап 10: проверка изолированной сборки домена.
//
// Программа линкуется только с chat_domain — ни application, ни
// infrastructure, ни обёртки брокера (CMakeLists.txt, chat_domain_isolated).
// Данные чата — свои, на std::map, написанные прямо здесь: настоящие движки
// живут в infrastructure, и их здесь нет. Если домен начнёт зависеть от
// внешнего слоя, эта цель перестанет собираться или линковаться.
//
// Удобные конструкторы BasicChatStore() и BasicChatStore(EngineKind) здесь
// не используются намеренно: они зовут фабрику make_chat_store, которая
// объявлена в домене, а определена в infrastructure (docs/layers.md).

#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "chat/chat_data.hpp"
#include "chat/chat_store.hpp"
#include "chat/events.hpp"
#include "chat/message.hpp"

namespace {

using Room = chat::ChatStore::Room;

class MapData final : public chat::IChatStore<Room> {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "map"; }
    [[nodiscard]] Room* find_room(std::string_view room) noexcept override {
        const auto it = rooms_.find(room);
        return it == rooms_.end() ? nullptr : &it->second;
    }
    [[nodiscard]] const Room* find_room(std::string_view room) const noexcept override {
        const auto it = rooms_.find(room);
        return it == rooms_.end() ? nullptr : &it->second;
    }
    Room& add_room(std::string name, Room room) override {
        return rooms_.emplace(std::move(name), std::move(room)).first->second;
    }
    [[nodiscard]] std::size_t room_count() const noexcept override { return rooms_.size(); }
    void for_each_room(const std::function<void(const std::string&, Room&)>& fn) override {
        for (auto& [name, room] : rooms_) {
            fn(name, room);
        }
    }
    [[nodiscard]] const UserPtr* find_user(std::string_view nick) const noexcept override {
        const auto it = users_.find(nick);
        return it == users_.end() ? nullptr : &it->second;
    }
    void add_user(std::string nick, UserPtr user) override { users_.emplace(std::move(nick), std::move(user)); }
    bool remove_user(std::string_view nick) noexcept override {
        const auto it = users_.find(nick);
        if (it == users_.end()) {
            return false;
        }
        users_.erase(it);
        return true;
    }
    void move_user(std::string_view from, std::string to) override {
        auto node = users_.extract(users_.find(from));
        node.key() = std::move(to);
        users_.insert(std::move(node));
    }
    [[nodiscard]] std::size_t user_count() const noexcept override { return users_.size(); }

private:
    std::map<std::string, Room, std::less<>> rooms_;
    std::map<std::string, UserPtr, std::less<>> users_;
};

int failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

}  // namespace

int main() {
    chat::ChatStore store(std::make_unique<MapData>());
    store.record_events(true);
    static_cast<void>(store.register_user("alice"));
    static_cast<void>(store.join("#general", "alice"));
    chat::MessagePtr message = chat::make_text("alice", "hello");
    message->assign_id(store.next_message_id("alice"));
    store.post("#general", std::move(message));
    static_cast<void>(store.leave("#general", "alice"));

    const std::vector<chat::DomainEvent> events = store.take_events();
    expect(events.size() == 3, "three events");
    if (events.size() == 3) {
        expect(std::holds_alternative<chat::UserJoined>(events[0]), "join first");
        const auto* const posted = std::get_if<chat::MessagePosted>(&events[1]);
        expect(posted != nullptr && posted->text == "hello" && posted->author == "alice", "message event");
        expect(std::holds_alternative<chat::UserLeft>(events[2]), "leave last");
    }
    if (failures == 0) {
        std::puts("domain builds and runs alone");
    }
    return failures == 0 ? 0 : 1;
}
