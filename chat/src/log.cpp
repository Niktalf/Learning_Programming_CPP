#include "chat/log.hpp"

#include <ios>

#include "chat/errors.hpp"

namespace chat {

AppendOnlyLog::AppendOnlyLog(const std::filesystem::path& path)
    : out_(path, std::ios::app | std::ios::binary) {
    if (!out_) {
        throw ChatError("cannot open log: " + path.string());
    }
}

void AppendOnlyLog::append(std::string_view record) {
    out_ << record << '\n';
    out_.flush();
    if (!out_) {
        throw ChatError("log write failed");
    }
}

}  // namespace chat
