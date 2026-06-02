#pragma once

#include <cstddef>
#include <cstdint>

namespace shredder {

// Aligned, mlock-attempted buffer that scrubs itself on destruction.
class SecureBuffer {
public:
    explicit SecureBuffer(std::size_t size, std::size_t alignment = 4096);
    ~SecureBuffer();

    SecureBuffer(const SecureBuffer&) = delete;
    SecureBuffer& operator=(const SecureBuffer&) = delete;
    SecureBuffer(SecureBuffer&& other) noexcept;
    SecureBuffer& operator=(SecureBuffer&& other) noexcept;

    [[nodiscard]] std::uint8_t* data() noexcept { return data_; }
    [[nodiscard]] const std::uint8_t* data() const noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool locked() const noexcept { return locked_; }

    void fill_random();
    void fill_pattern(std::uint8_t byte);
    void zero();

private:
    std::uint8_t* data_{nullptr};
    std::size_t size_{0};
    bool locked_{false};

    void release() noexcept;
};

} // namespace shredder
