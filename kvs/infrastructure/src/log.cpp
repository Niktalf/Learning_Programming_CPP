#include "kvs/log.hpp"

#include <ios>

#include "kvs/errors.hpp"
#include "kvs/file.hpp"

namespace kvs {

AppendOnlyLog::AppendOnlyLog(const std::filesystem::path& path)
    : out_(path, std::ios::app | std::ios::binary) {
    if (!out_) {
        throw KvError("cannot open log: " + path.string());
    }
}

void AppendOnlyLog::append(std::string_view record) {
    out_ << record << '\n';
    out_.flush();
    if (!out_) {
        throw KvError("log write failed");
    }
}

std::vector<std::string> FileHistory::records() const {
    if (!std::filesystem::exists(path_)) {
        return {};
    }
    return read_lines(path_);
}

}  // namespace kvs
