#include "safety.hpp"

#include <array>
#include <string_view>

namespace shredder {

namespace {

constexpr std::array<std::string_view, 7> kForbiddenPrefixes{{
    "/usr",
    "/bin",
    "/sbin",
    "/etc",
    "/System",
    "/Library",
    "/private/etc",
}};

bool has_prefix(const std::string& s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    return std::string_view(s.data(), prefix.size()) == prefix;
}

bool is_inside(const std::string& canonical, std::string_view dir) {
    if (canonical == dir) return true;
    return has_prefix(canonical, dir) && canonical.size() > dir.size() &&
           canonical[dir.size()] == '/';
}

} // namespace

SafetyResult check_path(const std::filesystem::path& path,
                        const SafetyOptions& opts) {
    if (path.empty()) {
        return {false, "empty path"};
    }
    for (const auto& part : path) {
        if (part == "..") {
            return {false, "path contains '..'"};
        }
    }

    if (opts.override_system_paths) {
        return {true, ""};
    }

    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(path, ec).string();
    if (ec) {
        canonical = path.string();
    }

    if (canonical == "/" || canonical.empty()) {
        return {false, "refusing to shred filesystem root"};
    }

    for (auto prefix : kForbiddenPrefixes) {
        if (is_inside(canonical, prefix)) {
            return {false, std::string("path inside protected system dir: ") +
                               std::string(prefix)};
        }
    }

    return {true, ""};
}

} // namespace shredder
