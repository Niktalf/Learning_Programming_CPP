#include "taskq/log.hpp"

#include <ios>

#include "taskq/errors.hpp"

namespace taskq {

AppendOnlyLog::AppendOnlyLog(const std::filesystem::path& path)
    : out_(path, std::ios::app | std::ios::binary) {
    if (!out_) {
        throw TaskQError("cannot open log: " + path.string());
    }
}

void AppendOnlyLog::append(std::string_view record) {
    out_ << record << '\n';
    out_.flush();
    if (!out_) {
        throw TaskQError("log write failed");
    }
}

}  // namespace taskq
