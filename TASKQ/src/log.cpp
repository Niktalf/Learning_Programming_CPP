#include "log.hpp"

#include <string>

#include "errors.hpp"

AppendOnlyLog::AppendOnlyLog(const std::filesystem::path& path) : out_(path, std::ios::app | std::ios::binary) {
    if (!out_) {
        throw TaskQError("cannot open log: " + path.string());
    }
}

void AppendOnlyLog::append(const std::string_view record) {
    out_ << record << '\n';
    out_.flush();
    if (!out_) {
        throw TaskQError("log write failed");
    }
}
