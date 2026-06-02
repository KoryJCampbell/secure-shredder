#include "progress.hpp"

#include <iomanip>
#include <ostream>
#include <sstream>

namespace shredder {

namespace {

std::string escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    std::ostringstream os;
                    os << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<int>(static_cast<unsigned char>(c));
                    out += os.str();
                } else {
                    out += c;
                }
        }
    }
    return out;
}

} // namespace

ProgressReporter::ProgressReporter(std::ostream& out, bool json_enabled)
    : out_(out), json_enabled_(json_enabled) {}

void ProgressReporter::emit(const std::string& line) {
    out_ << line << '\n';
    out_.flush();
}

void ProgressReporter::pass_started(int pass, int total_passes,
                                    std::string_view pattern) {
    if (!json_enabled_) return;
    std::ostringstream os;
    os << "{\"event\":\"pass_started\",\"pass\":" << pass
       << ",\"total_passes\":" << total_passes
       << ",\"pattern\":\"" << escape(pattern) << "\"}";
    emit(os.str());
}

void ProgressReporter::pass_progress(int pass, std::uint64_t bytes_written,
                                     std::uint64_t total_bytes,
                                     double speed_mbps) {
    if (!json_enabled_) return;
    std::ostringstream os;
    os << "{\"event\":\"progress\",\"pass\":" << pass
       << ",\"bytes_written\":" << bytes_written
       << ",\"total_bytes\":" << total_bytes
       << ",\"speed_mbps\":" << std::fixed << std::setprecision(2)
       << speed_mbps << "}";
    emit(os.str());
}

void ProgressReporter::pass_finished(int pass) {
    if (!json_enabled_) return;
    std::ostringstream os;
    os << "{\"event\":\"pass_finished\",\"pass\":" << pass << "}";
    emit(os.str());
}

void ProgressReporter::final_summary(const std::string& file,
                                     std::uint64_t size, int passes,
                                     const std::string& sha256,
                                     const std::string& status,
                                     std::uint64_t elapsed_ms) {
    std::ostringstream os;
    os << "{\"event\":\"summary\",\"file\":\"" << escape(file)
       << "\",\"size\":" << size << ",\"passes\":" << passes
       << ",\"sha256\":\"" << escape(sha256) << "\",\"status\":\""
       << escape(status) << "\",\"elapsed_ms\":" << elapsed_ms << "}";
    emit(os.str());
}

} // namespace shredder
