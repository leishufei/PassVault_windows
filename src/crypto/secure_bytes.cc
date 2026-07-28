#include "crypto/secure_bytes.h"

#include <algorithm>
#include <cstdlib>
#include <new>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace passvault::crypto {

namespace {

void SecureZero(void* p, std::size_t n) noexcept {
    if (p == nullptr || n == 0) return;
#ifdef _WIN32
    RtlSecureZeroMemory(p, n);
#else
    volatile std::uint8_t* v = static_cast<volatile std::uint8_t*>(p);
    while (n-- != 0) *v++ = 0;
#endif
}

}  // namespace

SecureBytes::SecureBytes(std::size_t size) {
    Allocate(size);
    size_ = size;
    if (size_ > 0) std::memset(data_, 0, size_);
}

SecureBytes::SecureBytes(const std::uint8_t* data, std::size_t size) {
    Assign(data, size);
}

SecureBytes::~SecureBytes() { Clear(); }

SecureBytes::SecureBytes(SecureBytes&& other) noexcept
    : data_(other.data_), size_(other.size_), capacity_(other.capacity_), locked_(other.locked_) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
    other.locked_ = false;
}

SecureBytes& SecureBytes::operator=(SecureBytes&& other) noexcept {
    if (this != &other) {
        Clear();
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        locked_ = other.locked_;
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
        other.locked_ = false;
    }
    return *this;
}

void SecureBytes::Allocate(std::size_t size) {
    if (size == 0) {
        ReleaseNoZero();
        return;
    }
    if (size <= capacity_) return;

    auto* new_buf = static_cast<std::uint8_t*>(std::malloc(size));
    if (new_buf == nullptr) throw std::bad_alloc();

    bool new_locked = false;
#ifdef _WIN32
    if (VirtualLock(new_buf, size)) {
        new_locked = true;
    }
#endif

    if (data_ != nullptr) {
        SecureZero(data_, size_);
        ReleaseNoZero();
    }
    data_ = new_buf;
    capacity_ = size;
    locked_ = new_locked;
}

void SecureBytes::Assign(const std::uint8_t* data, std::size_t size) {
    Allocate(size);
    size_ = size;
    if (size > 0 && data != nullptr) std::memcpy(data_, data, size);
}

void SecureBytes::AssignFromString(std::string_view s) {
    Assign(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}

void SecureBytes::Clear() noexcept {
    if (data_ != nullptr) {
        SecureZero(data_, capacity_);
        ReleaseNoZero();
    }
    size_ = 0;
}

void SecureBytes::ReleaseNoZero() noexcept {
#ifdef _WIN32
    if (locked_ && data_ != nullptr) {
        VirtualUnlock(data_, capacity_);
    }
#endif
    std::free(data_);
    data_ = nullptr;
    capacity_ = 0;
    locked_ = false;
}

bool SecureBytes::operator==(const SecureBytes& other) const noexcept {
    if (size_ != other.size_) return false;
    if (size_ == 0) return true;
    volatile std::uint8_t diff = 0;
    for (std::size_t i = 0; i < size_; ++i) {
        diff |= static_cast<std::uint8_t>(data_[i] ^ other.data_[i]);
    }
    return diff == 0;
}

}  // namespace passvault::crypto
