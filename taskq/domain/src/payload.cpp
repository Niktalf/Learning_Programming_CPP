#include "taskq/payload.hpp"

#include <memory>
#include <string>
#include <type_traits>

namespace taskq {

static_assert(std::has_virtual_destructor_v<IPayload>, "IPayload needs a virtual destructor");

// clone всех наследников устроен одинаково: копирующий конструктор
// конкретного класса, результат — в unique_ptr на базу. make_unique, а не
// new: ни одного new в рабочем коде. equals сначала сравнивает динамический
// тип: текст "42" и число 42 — разные тела.

PayloadPtr TextPayload::clone() const {
    return std::make_unique<TextPayload>(*this);
}

bool TextPayload::equals(const IPayload& other) const noexcept {
    const auto* const same = dynamic_cast<const TextPayload*>(&other);
    return same != nullptr && same->command_ == command_;
}

PayloadPtr NumericPayload::clone() const {
    return std::make_unique<NumericPayload>(*this);
}

bool NumericPayload::equals(const IPayload& other) const noexcept {
    const auto* const same = dynamic_cast<const NumericPayload*>(&other);
    return same != nullptr && same->value_ == value_;
}

std::string BinaryPayload::to_string() const {
    return '<' + std::to_string(bytes_.size()) + " bytes>";
}

PayloadPtr BinaryPayload::clone() const {
    return std::make_unique<BinaryPayload>(*this);
}

bool BinaryPayload::equals(const IPayload& other) const noexcept {
    const auto* const same = dynamic_cast<const BinaryPayload*>(&other);
    return same != nullptr && same->bytes_ == bytes_;
}

}  // namespace taskq
