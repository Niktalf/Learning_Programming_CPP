#include "ivalue.hpp"

#include <memory>
#include <sstream>
#include <type_traits>

static_assert(std::has_virtual_destructor_v<IValue>, "deletion via unique_ptr<IValue> requires a virtual destructor");

std::unique_ptr<IValue> StringValue::clone() const { return std::make_unique<StringValue>(*this); }
std::unique_ptr<IValue> IntValue::clone() const { return std::make_unique<IntValue>(*this); }
std::unique_ptr<IValue> FloatValue::clone() const { return std::make_unique<FloatValue>(*this); }
std::unique_ptr<IValue> ListValue::clone() const { return std::make_unique<ListValue>(*this); }

bool StringValue::equals(const IValue& other) const noexcept {
    const auto* o = dynamic_cast<const StringValue*>(&other);
    return o != nullptr && o->data_ == data_;
}

bool IntValue::equals(const IValue& other) const noexcept {
    const auto* o = dynamic_cast<const IntValue*>(&other);
    return o != nullptr && o->data_ == data_;
}

bool FloatValue::equals(const IValue& other) const noexcept {
    const auto* o = dynamic_cast<const FloatValue*>(&other);
    return o != nullptr && o->data_ == data_;
}

bool ListValue::equals(const IValue& other) const noexcept {
    const auto* o = dynamic_cast<const ListValue*>(&other);
    return o != nullptr && o->items_ == items_;
}

std::string IntValue::to_string() const {
    std::ostringstream out;
    out << data_;
    return out.str();
}

std::string FloatValue::to_string() const {
    std::ostringstream out;
    out << data_;
    return out.str();
}

std::string ListValue::to_string() const {
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < items_.size(); ++i) {
        out << (i == 0 ? "" : ", ") << items_[i];
    }
    out << ']';
    return out.str();
}
