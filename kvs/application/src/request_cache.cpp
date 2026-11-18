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

// Этап 8: удаление по условию — std::erase_if из C++20 вместо цикла с
// it = erase(it). Для ассоциативных контейнеров erase-remove неприменим:
// remove_if переставляет элементы, а ключи std::map переставлять нельзя;
// erase_if делает правильное для каждого контейнера и возвращает, сколько
// удалено.
std::size_t RequestCache::purge() {
    return std::erase_if(entries_, [](const auto& entry) { return entry.second.expired(); });
}

}  // namespace kvs
