#include "request_cache.hpp"

#include <memory>

std::shared_ptr<const Request> RequestCache::get(const std::string& text) {
    const auto it = entries_.find(text);
    if (it != entries_.end()) {
        if (auto alive = it->second.lock()) {
            return alive;
        }
    }
    auto request = std::make_shared<const Request>(text);
    entries_[text] = request;
    return request;
}

std::size_t RequestCache::purge() {
    std::size_t removed = 0;
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->second.expired()) {
            it = entries_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}
