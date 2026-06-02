#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "progress.hpp"
#include "safety.hpp"
#include "secure_buffer.hpp"
#include "sha256.hpp"
#include "shredder.hpp"

namespace fs = std::filesystem;
using namespace shredder;

namespace {

fs::path make_temp_file(std::size_t size_bytes, std::uint8_t fill_byte = 0xAA) {
    static std::atomic<std::uint64_t> counter{0};
    auto tmp = fs::temp_directory_path() /
               ("shredder_test_" + std::to_string(::getpid()) + "_" +
                std::to_string(counter.fetch_add(1)) + "_" +
                std::to_string(std::random_device{}()));
    std::ofstream out(tmp, std::ios::binary);
    std::vector<char> buf(4096, static_cast<char>(fill_byte));
    std::size_t remaining = size_bytes;
    while (remaining > 0) {
        auto chunk = std::min(remaining, buf.size());
        out.write(buf.data(), static_cast<std::streamsize>(chunk));
        remaining -= chunk;
    }
    out.close();
    return tmp;
}

} // namespace

TEST_CASE("SecureBuffer constructs and exposes data", "[secure]") {
    SecureBuffer buf(256);
    REQUIRE(buf.size() == 256);
    REQUIRE(buf.data() != nullptr);
    buf.fill_pattern(0xAB);
    REQUIRE(buf.data()[0] == 0xAB);
    REQUIRE(buf.data()[255] == 0xAB);
}

TEST_CASE("SecureBuffer fill_random changes bytes", "[secure]") {
    SecureBuffer buf(4096);
    buf.fill_pattern(0x00);
    buf.fill_random();
    int nonzero = 0;
    for (std::size_t i = 0; i < buf.size(); ++i) {
        if (buf.data()[i] != 0) ++nonzero;
    }
    // With 4096 random bytes the odds of all-zero are astronomically low.
    REQUIRE(nonzero > 4000);
}

TEST_CASE("SecureBuffer move transfers ownership", "[secure]") {
    SecureBuffer a(128);
    a.fill_pattern(0xCC);
    SecureBuffer b = std::move(a);
    REQUIRE(b.size() == 128);
    REQUIRE(a.size() == 0);
    REQUIRE(a.data() == nullptr);
    REQUIRE(b.data()[0] == 0xCC);
}

TEST_CASE("SHA-256 known vector for 'abc'", "[sha256]") {
    Sha256 h;
    const std::uint8_t msg[] = {'a','b','c'};
    h.update(msg, 3);
    auto hex = Sha256::to_hex(h.finalize());
    REQUIRE(hex ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("SHA-256 known vector for empty input", "[sha256]") {
    Sha256 h;
    auto hex = Sha256::to_hex(h.finalize());
    REQUIRE(hex ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("Safety blocks system paths", "[safety]") {
    SafetyOptions opts;
    REQUIRE_FALSE(check_path("/usr/bin/ls", opts).ok);
    REQUIRE_FALSE(check_path("/System/Library/Foo", opts).ok);
    REQUIRE_FALSE(check_path("/etc/passwd", opts).ok);
    REQUIRE_FALSE(check_path("/", opts).ok);
}

TEST_CASE("Safety blocks .. traversal", "[safety]") {
    SafetyOptions opts;
    REQUIRE_FALSE(check_path("/tmp/../etc/passwd", opts).ok);
}

TEST_CASE("Safety allows user temp paths", "[safety]") {
    SafetyOptions opts;
    auto tmp = fs::temp_directory_path() / "ok_file";
    REQUIRE(check_path(tmp, opts).ok);
}

TEST_CASE("Safety override bypasses system-path block", "[safety]") {
    SafetyOptions opts;
    opts.override_system_paths = true;
    REQUIRE(check_path("/etc/passwd", opts).ok);
}

TEST_CASE("Shred clear mode destroys file", "[shred]") {
    auto path = make_temp_file(16 * 1024);
    REQUIRE(fs::exists(path));

    std::ostringstream sink;
    ProgressReporter rep(sink, true);
    ShredOptions opts;
    opts.mode = ShredMode::Clear;
    auto result = shred_file(path, opts, rep);

    REQUIRE(result.status == "completed");
    REQUIRE(result.size == 16 * 1024);
    REQUIRE(result.passes == 1);
    REQUIRE_FALSE(fs::exists(path));
    REQUIRE(sink.str().find("\"event\":\"pass_started\"") != std::string::npos);
    REQUIRE(sink.str().find("\"event\":\"pass_finished\"") != std::string::npos);
}

TEST_CASE("Shred purge mode runs 3 passes", "[shred]") {
    auto path = make_temp_file(8 * 1024);
    std::ostringstream sink;
    ProgressReporter rep(sink, true);
    ShredOptions opts;
    opts.mode = ShredMode::Purge;
    auto result = shred_file(path, opts, rep);

    REQUIRE(result.passes == 3);
    REQUIRE_FALSE(fs::exists(path));
    REQUIRE(sink.str().find("\"pass\":3") != std::string::npos);
    REQUIRE(sink.str().find("\"pattern\":\"ones\"") != std::string::npos);
}

TEST_CASE("Pre-shred SHA-256 matches independent hash", "[shred]") {
    auto path = make_temp_file(1024, 0x42);
    auto expected = Sha256::hash_file(path);

    std::ostringstream sink;
    ProgressReporter rep(sink, false);
    ShredOptions opts;
    opts.mode = ShredMode::Clear;
    auto result = shred_file(path, opts, rep);

    REQUIRE(result.sha256_pre == expected);
    REQUIRE_FALSE(fs::exists(path));
}

TEST_CASE("JSON disabled emits no progress lines", "[progress]") {
    auto path = make_temp_file(4096);
    std::ostringstream sink;
    ProgressReporter rep(sink, false);
    ShredOptions opts;
    opts.mode = ShredMode::Clear;
    auto result = shred_file(path, opts, rep);
    REQUIRE(result.status == "completed");

    REQUIRE(sink.str().empty());
}

TEST_CASE("Final summary is always emitted", "[progress]") {
    std::ostringstream sink;
    ProgressReporter rep(sink, false);
    rep.final_summary("/tmp/x", 1024, 3, "deadbeef", "completed", 42);
    REQUIRE(sink.str().find("\"event\":\"summary\"") != std::string::npos);
    REQUIRE(sink.str().find("\"sha256\":\"deadbeef\"") != std::string::npos);
}
