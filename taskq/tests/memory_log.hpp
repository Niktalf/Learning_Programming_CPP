#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "taskq/journal.hpp"

namespace taskq::testing {

// Журнал в памяти — тестовая реализация порта ILogSink (journal.hpp).
// Записи общие у всех копий: сервер владеет своей копией, тест держит свою и
// видит, что записано. Ни одного файла.
class MemoryLog final : public ILogSink {
public:
    void append(std::string_view record) override { records_->emplace_back(record); }
    [[nodiscard]] const std::vector<std::string>& records() const noexcept { return *records_; }

    // Владеющая копия для сервера: unique_ptr на порт, записи — те же.
    [[nodiscard]] std::unique_ptr<ILogSink> sink() const { return std::make_unique<MemoryLog>(*this); }

private:
    std::shared_ptr<std::vector<std::string>> records_ = std::make_shared<std::vector<std::string>>();
};

}  // namespace taskq::testing
