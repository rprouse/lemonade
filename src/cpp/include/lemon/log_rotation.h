#pragma once

#include "lemon/utils/aixlog.hpp"

#include <cstdint>
#include <fstream>
#include <string>

namespace lemon {

// Rotates `path` into numbered backups (`path.1`, `path.2`, ...), pruning the
// oldest generation once `max_backups` is exceeded. No-op if `path` does not
// exist. Filesystem errors are swallowed (logged to stderr) so a locked
// backup file on Windows degrades to "rotation skipped this cycle" rather
// than crashing the caller.
void rotate_log_files(const std::string& path, int max_backups);

constexpr std::uintmax_t kDefaultLogMaxSizeBytes = 50ULL * 1024 * 1024;
constexpr int kDefaultLogMaxBackups = 5;

// File sink with size-triggered rotation: once the active file reaches
// max_size_bytes, it is rotated to numbered backups (see rotate_log_files)
// and a fresh file is started. Also rotates immediately at construction if
// the file already exceeds the threshold, so a pre-existing oversized log
// (e.g. from before this feature existed) is capped on the next startup
// rather than continuing to grow.
class FileLogSink : public AixLog::SinkFormat {
public:
    FileLogSink(const AixLog::Filter& filter,
                const std::string& filename,
                const std::string& format,
                std::uintmax_t max_size_bytes = kDefaultLogMaxSizeBytes,
                int max_backups = kDefaultLogMaxBackups);

    void log(const AixLog::Metadata& metadata, const std::string& message) override;

private:
    void check_and_rotate();

    std::string filename_;
    std::ofstream file_;
    std::uintmax_t max_size_bytes_;
    int max_backups_;
};

} // namespace lemon
