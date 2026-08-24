#include "lemon/log_rotation.h"

#include <filesystem>
#include <iostream>
#include <sstream>

namespace lemon {

namespace fs = std::filesystem;

FileLogSink::FileLogSink(const AixLog::Filter& filter,
                          const std::string& filename,
                          const std::string& format,
                          std::uintmax_t max_size_bytes,
                          int max_backups)
    : AixLog::SinkFormat(filter, format),
      filename_(filename),
      file_(filename.c_str(), std::ofstream::out | std::ofstream::app),
      max_size_bytes_(max_size_bytes),
      max_backups_(max_backups) {
    check_and_rotate();
}

void FileLogSink::log(const AixLog::Metadata& metadata, const std::string& message) {
    std::ostringstream stream;
    do_log(stream, metadata, message);

    std::string formatted = stream.str();
    if (!formatted.empty() && formatted.back() == '\n') {
        formatted.pop_back();
    }

    file_ << formatted << std::endl;
    file_.flush();

    check_and_rotate();
}

void FileLogSink::check_and_rotate() {
    std::error_code ec;
    std::uintmax_t size = fs::file_size(filename_, ec);
    if (ec || size < max_size_bytes_) {
        return;
    }

    file_.close();
    rotate_log_files(filename_, max_backups_);
    file_.open(filename_.c_str(), std::ofstream::out | std::ofstream::trunc);
}

void rotate_log_files(const std::string& path, int max_backups) {
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return;
    }

    try {
        fs::path oldest(path + "." + std::to_string(max_backups));
        if (fs::exists(oldest)) {
            fs::remove(oldest);
        }

        for (int i = max_backups - 1; i >= 1; --i) {
            fs::path src(path + "." + std::to_string(i));
            if (fs::exists(src)) {
                fs::rename(src, path + "." + std::to_string(i + 1));
            }
        }

        fs::rename(path, path + ".1");
    } catch (const fs::filesystem_error& e) {
        std::cerr << "[lemonade] log rotation failed for " << path << ": " << e.what() << std::endl;
    }
}

} // namespace lemon
