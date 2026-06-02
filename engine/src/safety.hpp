#pragma once

#include <filesystem>
#include <string>

namespace shredder {

struct SafetyOptions {
    bool override_system_paths = false;
};

struct SafetyResult {
    bool ok{false};
    std::string reason;
};

[[nodiscard]] SafetyResult check_path(const std::filesystem::path& path,
                                      const SafetyOptions& opts);

} // namespace shredder
