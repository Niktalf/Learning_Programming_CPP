#pragma once

#include <string>
#include <utility>
#include <vector>

namespace kvs {
namespace testing {

// Тип, записывающий в журнал каждый вызов конструктора и присваивания.
// Нужен звёздочке этапа 1: порядок вызовов при вставке в std::map виден
// и в консоли (утилита kvs_insert_order), и в тесте, который его фиксирует.
//
// Журнал — статическая переменная внутри функции: inline-переменные
// появятся только в C++17.
inline std::vector<std::string>& trace_log() {
    static std::vector<std::string> events;
    return events;
}

class Traced {
public:
    Traced() { trace_log().push_back("default"); }
    explicit Traced(std::string text) : text_(std::move(text)) {
        trace_log().push_back("construct");
    }

    Traced(const Traced& other) : text_(other.text_) { trace_log().push_back("copy"); }
    Traced(Traced&& other) noexcept : text_(std::move(other.text_)) {
        trace_log().push_back("move");
    }

    Traced& operator=(const Traced& other) {
        text_ = other.text_;
        trace_log().push_back("copy-assign");
        return *this;
    }

    Traced& operator=(Traced&& other) noexcept {
        text_ = std::move(other.text_);
        trace_log().push_back("move-assign");
        return *this;
    }

    ~Traced() = default;

    const std::string& text() const noexcept { return text_; }

private:
    std::string text_;
};

inline int count_events(const std::string& kind) {
    int count = 0;
    for (const std::string& event : trace_log()) {
        if (event == kind) {
            ++count;
        }
    }
    return count;
}

}  // namespace testing
}  // namespace kvs
