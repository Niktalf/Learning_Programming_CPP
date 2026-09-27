#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "value.hpp"

template <class V>
class BasicStore {
public:
    void set(std::string key, V value) {
        data_.insert_or_assign(std::move(key), std::move(value));
    }

    [[nodiscard]] std::optional<V> get(std::string_view key) const {
        typename Map::const_iterator it = data_.find(key);
        if (it == data_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    [[nodiscard]] bool contains(std::string_view key) const noexcept {
        return data_.find(key) != data_.end();
    }

    bool del(std::string_view key) {
        typename Map::iterator it = data_.find(key);
        if (it == data_.end()) {
            return false;
        }
        data_.erase(it);
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }

    template <class F>
    void for_each(F fn) const {
        for (const auto& [key, value] : data_) {
            fn(key, value);
        }
    }

private:
    using Map = std::map<std::string, V, std::less<>>;
    Map data_;
};

using Store = BasicStore<Value>;
