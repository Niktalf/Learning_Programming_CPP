#include "kvs/request_cache.hpp"

#include <memory>
#include <string>

namespace kvs {

std::shared_ptr<const Request> RequestCache::get(const std::string& text) {
    // lock() атомарно проверяет, жив ли объект, и если жив — продлевает ему
    // жизнь новым shared_ptr. Проверка expired() с последующим lock() была бы
    // гонкой на этапе 11: между ними объект мог бы умереть.
    if (const auto it = entries_.find(text); it != entries_.end()) {
        if (std::shared_ptr<const Request> alive = it->second.lock()) {
            ++hits_;
            return alive;
        }
    }

    // make_shared: объект и контрольный блок — одна аллокация. Обратная
    // сторона: деструктор запроса вызывается, как только умер последний
    // shared_ptr, но сам блок памяти освобождается, только когда умрёт и
    // последний weak_ptr — то есть когда запись удалит purge. Ещё одна
    // причина, по которой purge обязателен.
    auto created = std::make_shared<const Request>(text);
    if (entries_.size() >= purge_threshold_) {
        purge();
    }
    entries_.insert_or_assign(text, created);
    return created;
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

}  // namespace kvs
