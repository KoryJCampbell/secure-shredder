#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace shredder {

class Sha256 {
public:
    Sha256();
    void update(const std::uint8_t* data, std::size_t len);
    std::array<std::uint8_t, 32> finalize();

    static std::string to_hex(const std::array<std::uint8_t, 32>& digest);
    static std::string hash_file(const std::filesystem::path& path);

private:
    std::uint32_t state_[8]{};
    std::uint64_t bit_count_{0};
    std::uint8_t buffer_[64]{};
    std::size_t buffer_len_{0};

    void transform(const std::uint8_t* block);
};

} // namespace shredder
