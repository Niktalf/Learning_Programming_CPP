#include "ipayload.hpp"

#include <sstream>
#include <type_traits>

static_assert(std::has_virtual_destructor_v<IPayload>, "deletion via unique_ptr<IPayload> requires a virtual destructor");

std::unique_ptr<IPayload> TextPayload::clone() const { return std::make_unique<TextPayload>(*this); }
std::unique_ptr<IPayload> NumericPayload::clone() const { return std::make_unique<NumericPayload>(*this); }
std::unique_ptr<IPayload> BinaryPayload::clone() const { return std::make_unique<BinaryPayload>(*this); }

bool TextPayload::equals(const IPayload& other) const noexcept {
    const auto* o = dynamic_cast<const TextPayload*>(&other);
    return o != nullptr && o->command_ == command_;
}

bool NumericPayload::equals(const IPayload& other) const noexcept {
    const auto* o = dynamic_cast<const NumericPayload*>(&other);
    return o != nullptr && o->value_ == value_;
}

bool BinaryPayload::equals(const IPayload& other) const noexcept {
    const auto* o = dynamic_cast<const BinaryPayload*>(&other);
    return o != nullptr && o->bytes_ == bytes_;
}

std::string NumericPayload::to_string() const {
    std::ostringstream out;
    out << value_;
    return out.str();
}

std::string BinaryPayload::to_string() const {
    std::ostringstream out;
    out << '<' << bytes_.size() << " bytes>";
    return out.str();
}
