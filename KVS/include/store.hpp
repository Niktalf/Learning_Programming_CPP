#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <utility>

#include "value.hpp"

template <class V>
class BasicStore {
public:
    void set(std::string key, V value) {
        typename Map::iterator it = data_.find(key);
        if (it != data_.end()) {
            it->second = std::move(value);
        } else {
            data_.insert(std::make_pair(std::move(key), std::move(value)));
        }
    }

    const V* get(const std::string& key) const noexcept {
        typename Map::const_iterator it = data_.find(key);
        return it == data_.end() ? nullptr : &it->second;
    }

    bool del(const std::string& key) {
        typename Map::iterator it = data_.find(key);
        if (it == data_.end()) {
            return false;
        }
        data_.erase(it);
        return true;
    }

    std::size_t size() const noexcept { return data_.size(); }
    bool empty() const noexcept { return data_.empty(); }

    template <class F>
    void for_each(F fn) const {
        for (const auto& entry : data_) {
            fn(entry.first, entry.second);
        }
    }

private:
    using Map = std::map<std::string, V>;
    Map data_;
};

using Store = BasicStore<Value>;
