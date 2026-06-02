#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "progress.hpp"
#include "safety.hpp"
#include "shredder.hpp"

namespace {

void print_usage() {
    std::cerr <<
        "shredder " << "0.1.0" << " - NIST 800-88 file sanitizer\n\n"
        "usage: shredder --mode <clear|purge|destroy> [options] <path>\n\n"
        "options:\n"
        "  --mode <m>            clear (1 pass) | purge (3 pass) | destroy (35 pass)\n"
        "  --json-progress       emit one JSON object per progress event\n"
        "  --block-size <n>      I/O block size in bytes (default 4096)\n"
        "  --i-will-not-sue-you  bypass system-path safety guard\n"
        "  -h, --help            show this help\n";
}

} // namespace

int main(int argc, char* argv[]) {
    using namespace shredder;

    std::string mode_str = "purge";
    bool json_progress = false;
    std::size_t block_size = 4096;
    bool override_system = false;
    std::string path_arg;

    std::vector<std::string_view> args(argv + 1, argv + argc);
    for (std::size_t i = 0; i < args.size(); ++i) {
        auto a = args[i];
        if (a == "--mode") {
            if (i + 1 >= args.size()) { print_usage(); return 2; }
            mode_str = std::string(args[++i]);
        } else if (a == "--json-progress") {
            json_progress = true;
        } else if (a == "--block-size") {
            if (i + 1 >= args.size()) { print_usage(); return 2; }
            block_size = static_cast<std::size_t>(
                std::stoul(std::string(args[++i])));
        } else if (a == "--i-will-not-sue-you") {
            override_system = true;
        } else if (a == "-h" || a == "--help") {
            print_usage();
            return 0;
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "unknown flag: " << a << '\n';
            print_usage();
            return 2;
        } else {
            path_arg = std::string(a);
        }
    }

    if (path_arg.empty()) {
        print_usage();
        return 2;
    }

    try {
        SafetyOptions safety_opts{override_system};
        auto check = check_path(path_arg, safety_opts);
        if (!check.ok) {
            std::cerr << "refused: " << check.reason << '\n';
            return 3;
        }

        ShredOptions opts;
        opts.mode = parse_mode(mode_str);
        opts.block_size = block_size;

        ProgressReporter reporter(std::cout, json_progress);
        auto result = shred_file(path_arg, opts, reporter);
        reporter.final_summary(result.file, result.size, result.passes,
                               result.sha256_pre, result.status,
                               result.elapsed_ms);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
