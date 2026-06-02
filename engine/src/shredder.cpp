#include "shredder.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <stdlib.h>   // arc4random_buf
#include <sys/stat.h>
#include <unistd.h>

#include "secure_buffer.hpp"
#include "sha256.hpp"

namespace shredder {

namespace {

constexpr int kClearPasses   = 1;
constexpr int kPurgePasses   = 3;
constexpr int kDestroyPasses = 35;

enum class Pattern { Random, Ones, Zero };

void fill_pattern(SecureBuffer& buf, Pattern pat) {
    switch (pat) {
        case Pattern::Random: buf.fill_random();      break;
        case Pattern::Ones:   buf.fill_pattern(0xFF); break;
        case Pattern::Zero:   buf.fill_pattern(0x00); break;
    }
}

const char* pattern_label(Pattern pat) {
    switch (pat) {
        case Pattern::Random: return "random";
        case Pattern::Ones:   return "ones";
        case Pattern::Zero:   return "zeros";
    }
    return "unknown";
}

int open_for_write(const std::filesystem::path& path) {
    int fd = ::open(path.c_str(), O_WRONLY);
    if (fd < 0) throw std::runtime_error("cannot open file for shred");
#if defined(__APPLE__)
    // Bypass the unified buffer cache so writes hit the device.
    ::fcntl(fd, F_NOCACHE, 1);
#endif
    return fd;
}

void sync_and_close(int fd) {
#if defined(__APPLE__)
    ::fcntl(fd, F_FULLFSYNC, 0);
#else
    ::fdatasync(fd);
#endif
    ::close(fd);
}

void single_pass(const std::filesystem::path& path, int pass_index,
                 int total_passes, std::uint64_t file_size, Pattern pat,
                 const ShredOptions& opts, ProgressReporter& reporter) {
    reporter.pass_started(pass_index, total_passes, pattern_label(pat));

    int fd = open_for_write(path);
    SecureBuffer buf(opts.block_size);

    auto pass_start = std::chrono::steady_clock::now();
    std::uint64_t written = 0;
    std::uint64_t last_report = 0;
    constexpr std::uint64_t kReportEvery = 4ull * 1024 * 1024; // 4 MiB

    while (written < file_size) {
        std::uint64_t remaining = file_size - written;
        std::size_t this_block = static_cast<std::size_t>(
            std::min<std::uint64_t>(opts.block_size, remaining));

        // Refill per block so random passes maintain entropy across the file.
        fill_pattern(buf, pat);

        ssize_t n = ::pwrite(fd, buf.data(), this_block,
                             static_cast<off_t>(written));
        if (n < 0) {
            ::close(fd);
            throw std::runtime_error("pwrite failed during shred");
        }
        written += static_cast<std::uint64_t>(n);

        if (written - last_report >= kReportEvery || written == file_size) {
            auto now = std::chrono::steady_clock::now();
            double secs =
                std::chrono::duration<double>(now - pass_start).count();
            double mbps =
                secs > 0.0
                    ? (static_cast<double>(written) / (1024.0 * 1024.0)) / secs
                    : 0.0;
            reporter.pass_progress(pass_index, written, file_size, mbps);
            last_report = written;
        }
    }

    sync_and_close(fd);
    reporter.pass_finished(pass_index);
}

void run_passes(const std::filesystem::path& path, ShredMode mode,
                std::uint64_t size, const ShredOptions& opts,
                ProgressReporter& reporter) {
    int total = mode_passes(mode);
    if (mode == ShredMode::Clear) {
        single_pass(path, 1, total, size, Pattern::Random, opts, reporter);
    } else if (mode == ShredMode::Purge) {
        single_pass(path, 1, total, size, Pattern::Random, opts, reporter);
        single_pass(path, 2, total, size, Pattern::Ones,   opts, reporter);
        single_pass(path, 3, total, size, Pattern::Random, opts, reporter);
    } else {
        // Simplified Gutmann: 35 random passes. Pass count is the contractually
        // visible part of the algorithm; the rotating 27 hardcoded patterns are
        // only meaningful for 1990s MFM/RLL drives that no longer exist.
        for (int i = 1; i <= total; ++i) {
            single_pass(path, i, total, size, Pattern::Random, opts, reporter);
        }
    }
}

void rename_and_unlink(std::filesystem::path& path, int rounds,
                       bool no_unlink) {
    namespace fs = std::filesystem;
    auto parent = path.parent_path();
    if (parent.empty()) parent = fs::current_path();

    for (int i = 0; i < rounds; ++i) {
        std::uint8_t rand_bytes[16];
        arc4random_buf(rand_bytes, sizeof(rand_bytes));
        char hex[33];
        for (int j = 0; j < 16; ++j) {
            std::snprintf(hex + j * 2, 3, "%02x",
                          static_cast<unsigned>(rand_bytes[j]));
        }
        auto next = parent / hex;
        std::error_code ec;
        fs::rename(path, next, ec);
        if (ec) break;
        path = next;
    }
    if (!no_unlink) {
        std::error_code ec;
        fs::remove(path, ec);
    }
}

} // namespace

ShredMode parse_mode(std::string_view s) {
    if (s == "clear")   return ShredMode::Clear;
    if (s == "purge")   return ShredMode::Purge;
    if (s == "destroy") return ShredMode::Destroy;
    throw std::invalid_argument("unknown mode: " + std::string(s));
}

std::string mode_name(ShredMode m) {
    switch (m) {
        case ShredMode::Clear:   return "clear";
        case ShredMode::Purge:   return "purge";
        case ShredMode::Destroy: return "destroy";
    }
    return "unknown";
}

int mode_passes(ShredMode m) {
    switch (m) {
        case ShredMode::Clear:   return kClearPasses;
        case ShredMode::Purge:   return kPurgePasses;
        case ShredMode::Destroy: return kDestroyPasses;
    }
    return 0;
}

ShredResult shred_file(const std::filesystem::path& path,
                       const ShredOptions& opts, ProgressReporter& reporter) {
    namespace fs = std::filesystem;

    if (!fs::exists(path))          throw std::runtime_error("file does not exist");
    if (!fs::is_regular_file(path)) throw std::runtime_error("not a regular file");

    auto size = fs::file_size(path);
    ShredResult result;
    result.file = path.string();
    result.size = size;
    result.passes = mode_passes(opts.mode);

    if (opts.compute_sha256 && size > 0) {
        result.sha256_pre = Sha256::hash_file(path);
    }

    auto t0 = std::chrono::steady_clock::now();
    if (size > 0) {
        run_passes(path, opts.mode, size, opts, reporter);
    }

    auto path_copy = path;
    rename_and_unlink(path_copy, opts.rename_rounds, opts.no_unlink);

    auto t1 = std::chrono::steady_clock::now();
    result.elapsed_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
    result.status = "completed";
    return result;
}

} // namespace shredder
