#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kvs/journal.hpp"

namespace kvs::testing {

// Журнал в памяти — тестовая реализация обоих портов (journal.hpp). Записи
// общие у всех копий: сервер владеет своей копией как ILogSink, тест держит
// свою и видит, что записано, а при «перезапуске» отдаёт её же новому
// серверу как ILogSource. Ни одного файла.
class MemoryLog final : public ILogSink, public ILogSource {
public:
    MemoryLog() = default;
    explicit MemoryLog(std::vector<std::string> records)
        : records_(std::make_shared<std::vector<std::string>>(std::move(records))) {}

    void append(std::string_view record) override { records_->emplace_back(record); }
    [[nodiscard]] std::vector<std::string> records() const override { return *records_; }
    [[nodiscard]] std::string name() const override { return "memory"; }

    // Владеющая копия для сервера: unique_ptr на порт, записи — те же.
    [[nodiscard]] std::unique_ptr<ILogSink> sink() const { return std::make_unique<MemoryLog>(*this); }

private:
    std::shared_ptr<std::vector<std::string>> records_ = std::make_shared<std::vector<std::string>>();
};

}  // namespace kvs::testing
