#include "secure_buffer.hpp"

#include <cstdlib>
#include <cstring>
#include <new>
#include <stdlib.h>   // arc4random_buf on macOS/BSD
#include <sys/mman.h> // mlock/munlock

namespace shredder {

namespace {

// Best-effort secure zero that the optimizer can't elide.
// Portable approach: volatile-pointer write loop. The volatile qualifier
// forbids dead-store elimination even when the buffer is about to be freed.
void secure_zero(void* p, std::size_t n) noexcept {
    auto* vp = static_cast<volatile std::uint8_t*>(p);
    for (std::size_t i = 0; i < n; ++i) {
        vp[i] = 0;
    }
}

} // namespace

SecureBuffer::SecureBuffer(std::size_t size, std::size_t alignment)
    : size_(size) {
    void* raw = nullptr;
    if (posix_memalign(&raw, alignment, size) != 0 || raw == nullptr) {
        throw std::bad_alloc{};
    }
    data_ = static_cast<std::uint8_t*>(raw);
    // mlock can fail without root; treat as best-effort.
    if (mlock(data_, size_) == 0) {
        locked_ = true;
    }
}

SecureBuffer::~SecureBuffer() { release(); }

SecureBuffer::SecureBuffer(SecureBuffer&& other) noexcept
    : data_(other.data_), size_(other.size_), locked_(other.locked_) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.locked_ = false;
}

SecureBuffer& SecureBuffer::operator=(SecureBuffer&& other) noexcept {
    if (this != &other) {
        release();
        data_ = other.data_;
        size_ = other.size_;
        locked_ = other.locked_;
        other.data_ = nullptr;
        other.size_ = 0;
        other.locked_ = false;
    }
    return *this;
}

void SecureBuffer::release() noexcept {
    if (data_ == nullptr) return;
    secure_zero(data_, size_);
    if (locked_) {
        munlock(data_, size_);
        locked_ = false;
    }
    std::free(data_);
    data_ = nullptr;
    size_ = 0;
}

void SecureBuffer::fill_random() {
    arc4random_buf(data_, size_);
}

void SecureBuffer::fill_pattern(std::uint8_t byte) {
    std::memset(data_, byte, size_);
}

void SecureBuffer::zero() { secure_zero(data_, size_); }

} // namespace shredder
