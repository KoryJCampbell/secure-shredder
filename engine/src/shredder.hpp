#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "progress.hpp"

namespace shredder {

enum class ShredMode {
    Clear,    // 1 pass random
    Purge,    // NIST 800-88: random, 0xFF, random
    Destroy,  // 35-pass random (Gutmann pass count)
};

[[nodiscard]] ShredMode parse_mode(std::string_view s);
[[nodiscard]] std::string mode_name(ShredMode m);
[[nodiscard]] int mode_passes(ShredMode m);

struct ShredOptions {
    ShredMode mode = ShredMode::Purge;
    std::size_t block_size = 4096;
    int rename_rounds = 5;
    bool compute_sha256 = true;
    bool no_unlink = false; // for tests that want to keep the renamed file
};

struct ShredResult {
    std::string file;
    std::uint64_t size{0};
    int passes{0};
    std::string sha256_pre;
    std::string status;
    std::uint64_t elapsed_ms{0};
};

[[nodiscard]] ShredResult shred_file(const std::filesystem::path& path,
                                     const ShredOptions& opts,
                                     ProgressReporter& reporter);

} // namespace shredder
