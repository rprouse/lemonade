#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <lemon/log_rotation.h>

namespace fs = std::filesystem;
using lemon::FileLogSink;

static int passed = 0;
static int failures = 0;

static void check(bool cond, const char* desc) {
    if (cond) {
        std::fprintf(stderr, "[PASS] %s\n", desc);
        ++passed;
    } else {
        std::fprintf(stderr, "[FAIL] %s\n", desc);
        ++failures;
    }
    std::fflush(stderr);
}

class ScopedTempDir {
public:
    ScopedTempDir() {
        path_ = fs::temp_directory_path() /
                ("lemon_test_log_rotation_" + std::to_string(std::rand()) + "_" +
                 std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
        fs::create_directories(path_);
    }
    ~ScopedTempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    fs::path file(const std::string& name) const { return path_ / name; }

private:
    fs::path path_;
};

static void write_file(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ofstream::out | std::ofstream::trunc);
    out << content;
}

static std::string read_file(const fs::path& path) {
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// ============================================================================
// lemon::rotate_log_files — pure numbered-suffix shifting logic
// ============================================================================

static void test_rotate_creates_first_backup() {
    std::puts("--- test_rotate_creates_first_backup ---");
    ScopedTempDir dir;
    fs::path active = dir.file("app.log");
    write_file(active, "active-content");

    lemon::rotate_log_files(active.string(), 5);

    check(!fs::exists(active), "active file renamed away after rotation");
    check(fs::exists(dir.file("app.log.1")), "app.log.1 created");
    check(read_file(dir.file("app.log.1")) == "active-content", "app.log.1 has the rotated content");
}

static void test_rotate_noop_when_source_missing() {
    std::puts("\n--- test_rotate_noop_when_source_missing ---");
    ScopedTempDir dir;
    fs::path active = dir.file("missing.log");

    lemon::rotate_log_files(active.string(), 5);

    check(!fs::exists(dir.file("missing.log.1")), "no backup created when source is missing");
}

static void test_rotate_shifts_and_prunes() {
    std::puts("\n--- test_rotate_shifts_and_prunes ---");
    ScopedTempDir dir;
    fs::path active = dir.file("app.log");

    // 5 rotations with max_backups=3 should keep only the 3 most recent
    // generations and never create a 4th backup file.
    for (int i = 0; i < 5; ++i) {
        write_file(active, "gen" + std::to_string(i));
        lemon::rotate_log_files(active.string(), 3);
    }

    check(read_file(dir.file("app.log.1")) == "gen4", "app.log.1 holds the most recent generation");
    check(read_file(dir.file("app.log.2")) == "gen3", "app.log.2 holds the next generation");
    check(read_file(dir.file("app.log.3")) == "gen2", "app.log.3 holds the oldest retained generation");
    check(!fs::exists(dir.file("app.log.4")), "app.log.4 never created (older generations pruned)");
}

// ============================================================================
// lemon::FileLogSink — size-triggered rotation wiring
// ============================================================================

static void test_filesink_rotates_after_threshold_exceeded() {
    std::puts("\n--- test_filesink_rotates_after_threshold_exceeded ---");
    ScopedTempDir dir;
    fs::path active = dir.file("server.log");

    AixLog::Filter filter(AixLog::Severity::trace);
    // A single 10-char message is at most 12 bytes on disk (10 + CRLF); two of
    // them are at least 20. Threshold of 13 guarantees the 1st write never
    // rotates but the 2nd always does, regardless of platform line endings.
    FileLogSink sink(filter, active.string(), "#message", /*max_size_bytes=*/13, /*max_backups=*/5);

    sink.log(AixLog::Metadata{}, "AAAAAAAAAA");
    check(!fs::exists(dir.file("server.log.1")), "no rotation after first short write");

    sink.log(AixLog::Metadata{}, "AAAAAAAAAA");
    check(fs::exists(dir.file("server.log.1")), "rotation happens once threshold is crossed");

    std::string backup = read_file(dir.file("server.log.1"));
    check(backup.find("AAAAAAAAAA") != std::string::npos, "rotated backup retains the pre-rotation content");
    check(fs::file_size(active) == 0, "active file is truncated immediately after rotation");
}

static void test_filesink_rotates_immediately_if_already_oversized() {
    std::puts("\n--- test_filesink_rotates_immediately_if_already_oversized ---");
    ScopedTempDir dir;
    fs::path active = dir.file("server.log");
    write_file(active, std::string(50, 'X'));

    AixLog::Filter filter(AixLog::Severity::trace);
    FileLogSink sink(filter, active.string(), "#message", /*max_size_bytes=*/13, /*max_backups=*/5);

    check(fs::exists(dir.file("server.log.1")), "pre-existing oversized file rotated at construction");
    check(read_file(dir.file("server.log.1")) == std::string(50, 'X'),
          "rotated backup preserves the pre-existing content");
    check(fs::exists(active) && fs::file_size(active) == 0, "a fresh empty active file is ready for writes");
}

static void test_filesink_respects_max_backups() {
    std::puts("\n--- test_filesink_respects_max_backups ---");
    ScopedTempDir dir;
    fs::path active = dir.file("server.log");

    AixLog::Filter filter(AixLog::Severity::trace);
    // max_size_bytes=1 means every single write rotates immediately.
    FileLogSink sink(filter, active.string(), "#message", /*max_size_bytes=*/1, /*max_backups=*/2);

    sink.log(AixLog::Metadata{}, "1");
    sink.log(AixLog::Metadata{}, "2");
    sink.log(AixLog::Metadata{}, "3");

    check(read_file(dir.file("server.log.1")).find('3') != std::string::npos, "backup .1 holds the newest write");
    check(read_file(dir.file("server.log.2")).find('2') != std::string::npos, "backup .2 holds the prior write");
    check(!fs::exists(dir.file("server.log.3")), "backup .3 never created (max_backups=2)");
}

int main() {
    std::puts("=== Log Rotation Unit Tests ===\n");

    test_rotate_creates_first_backup();
    test_rotate_noop_when_source_missing();
    test_rotate_shifts_and_prunes();

    test_filesink_rotates_after_threshold_exceeded();
    test_filesink_rotates_immediately_if_already_oversized();
    test_filesink_respects_max_backups();

    std::printf("\n================================================\n");
    if (failures > 0) {
        std::printf("Tests finished: %d FAILURE(S)\n", failures);
        return 1;
    }
    std::printf("All log rotation tests PASSED (%d passed).\n", passed);
    return 0;
}
