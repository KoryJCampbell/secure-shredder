#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>

namespace shredder {

class ProgressReporter {
public:
    ProgressReporter(std::ostream& out, bool json_enabled);

    void pass_started(int pass, int total_passes, std::string_view pattern);
    void pass_progress(int pass, std::uint64_t bytes_written,
                       std::uint64_t total_bytes, double speed_mbps);
    void pass_finished(int pass);

    // Always emitted regardless of json_enabled (final result is the contract).
    void final_summary(const std::string& file, std::uint64_t size, int passes,
                       const std::string& sha256, const std::string& status,
                       std::uint64_t elapsed_ms);

private:
    std::ostream& out_;
    bool json_enabled_;

    void emit(const std::string& line);
};

} // namespace shredder
