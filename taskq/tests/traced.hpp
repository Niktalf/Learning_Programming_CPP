#pragma once

#include <string>
#include <utility>
#include <vector>

namespace taskq {
namespace testing {

// Тип, записывающий в журнал каждый вызов конструктора и присваивания.
// Нужен звёздочке этапа 1: порядок вызовов при добавлении в std::vector виден
// и в консоли (утилита taskq_push_order), и в тесте, который его фиксирует.
//
// Журнал — статическая переменная внутри функции: inline-переменные
// появятся только в C++17.
inline std::vector<std::string>& trace_log() {
    static std::vector<std::string> events;
    return events;
}

inline int count_events(const std::string& kind) {
    int count = 0;
    for (const std::string& event : trace_log()) {
        if (event == kind) {
            ++count;
        }
    }
    return count;
}

// Параметр шаблона решает, помечен ли перемещающий конструктор noexcept.
// Это и есть предмет звёздочки: от одного слова зависит, будет ли вектор при
// перевыделении перемещать накопленные элементы или копировать их.
template <bool NoexceptMove>
class BasicTraced {
public:
    BasicTraced() { trace_log().push_back("default"); }
    explicit BasicTraced(std::string text) : text_(std::move(text)) {
        trace_log().push_back("construct");
    }

    BasicTraced(const BasicTraced& other) : text_(other.text_) {
        trace_log().push_back("copy");
    }
    BasicTraced(BasicTraced&& other) noexcept(NoexceptMove)
        : text_(std::move(other.text_)) {
        trace_log().push_back("move");
    }

    BasicTraced& operator=(const BasicTraced& other) {
        text_ = other.text_;
        trace_log().push_back("copy-assign");
        return *this;
    }

    BasicTraced& operator=(BasicTraced&& other) noexcept(NoexceptMove) {
        text_ = std::move(other.text_);
        trace_log().push_back("move-assign");
        return *this;
    }

    ~BasicTraced() = default;

    const std::string& text() const noexcept { return text_; }

private:
    std::string text_;
};

using Traced = BasicTraced<true>;
using TracedThrowingMove = BasicTraced<false>;

}  // namespace testing
}  // namespace taskq
