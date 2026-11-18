// Этап 10: проверка изолированной сборки домена.
//
// Программа линкуется только с kvs_domain — ни application, ни
// infrastructure, ни lab::kafka (CMakeLists.txt, kvs_domain_isolated).
// Движок — свой, на std::map, написанный прямо здесь: настоящие движки живут
// в infrastructure, и их здесь нет. Если домен начнёт зависеть от внешнего
// слоя, эта цель перестанет собираться или линковаться, и ctest это покажет.
//
// Удобные конструкторы BasicStore() и BasicStore(EngineKind) здесь не
// используются намеренно: они зовут фабрику make_engine, которая объявлена
// в домене, а определена в infrastructure (docs/layers.md).

#include <cstdio>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "kvs/events.hpp"
#include "kvs/storage_engine.hpp"
#include "kvs/store.hpp"
#include "kvs/value.hpp"

namespace {

using Entry = kvs::StoreEntry<kvs::Value>;

// Простейший движок: одна «корзина», позиция — номер элемента в порядке ключей.
class MapEngine final : public kvs::IStorageEngine<Entry> {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "map"; }
    [[nodiscard]] std::unique_ptr<IStorageEngine> clone() const override {
        return std::make_unique<MapEngine>(*this);
    }
    [[nodiscard]] Entry* find(std::string_view key) noexcept override {
        const auto it = table_.find(key);
        return it == table_.end() ? nullptr : &it->second;
    }
    [[nodiscard]] const Entry* find(std::string_view key) const noexcept override {
        const auto it = table_.find(key);
        return it == table_.end() ? nullptr : &it->second;
    }
    bool put(std::string key, Entry entry) override {
        return table_.insert_or_assign(std::move(key), std::move(entry)).second;
    }
    bool erase(std::string_view key) noexcept override {
        const auto it = table_.find(key);
        if (it == table_.end()) {
            return false;
        }
        table_.erase(it);
        return true;
    }
    [[nodiscard]] std::size_t size() const noexcept override { return table_.size(); }
    [[nodiscard]] std::size_t bucket_count() const noexcept override { return 1; }
    [[nodiscard]] std::optional<Slot> first_from(std::size_t bucket) const noexcept override {
        if (bucket > 0 || table_.empty()) {
            return std::nullopt;
        }
        return Slot{0, 0};
    }
    [[nodiscard]] std::optional<Slot> next(Slot slot) const noexcept override {
        if (slot.index + 1 >= table_.size()) {
            return std::nullopt;
        }
        return Slot{0, slot.index + 1};
    }
    [[nodiscard]] const std::string& key_at(Slot slot) const noexcept override { return at(slot)->first; }
    [[nodiscard]] const Entry& entry_at(Slot slot) const noexcept override { return at(slot)->second; }

private:
    using Table = std::map<std::string, Entry, std::less<>>;

    [[nodiscard]] Table::const_iterator at(Slot slot) const noexcept {
        return std::next(table_.begin(), static_cast<std::ptrdiff_t>(slot.index));
    }

    Table table_;
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
    kvs::Store store(std::make_unique<MapEngine>());
    store.record_events(true);
    store.set("a", kvs::Value(std::string("1")));
    store.set("a", kvs::Value(std::string("2")));
    static_cast<void>(store.del("a"));

    const std::vector<kvs::DomainEvent> events = store.take_events();
    expect(events.size() == 3, "three events");
    if (events.size() == 3) {
        const auto* const second = std::get_if<kvs::KeyChanged>(&events[1]);
        expect(second != nullptr && second->generation == 2, "second write has generation 2");
        expect(std::holds_alternative<kvs::KeyDeleted>(events[2]), "delete event last");
    }
    expect(store.take_events().empty(), "events are taken once");
    if (failures == 0) {
        std::puts("domain builds and runs alone");
    }
    return failures == 0 ? 0 : 1;
}
