#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace passvault::crypto {

class SecureBytes {
 public:
    SecureBytes() = default;
    explicit SecureBytes(std::size_t size);
    SecureBytes(const std::uint8_t* data, std::size_t size);

    ~SecureBytes();

    SecureBytes(const SecureBytes&) = delete;
    SecureBytes& operator=(const SecureBytes&) = delete;

    SecureBytes(SecureBytes&& other) noexcept;
    SecureBytes& operator=(SecureBytes&& other) noexcept;

    void Assign(const std::uint8_t* data, std::size_t size);
    void AssignFromString(std::string_view s);

    void Clear() noexcept;

    std::uint8_t* data() noexcept { return data_; }
    const std::uint8_t* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }

    bool operator==(const SecureBytes& other) const noexcept;
    bool operator!=(const SecureBytes& other) const noexcept { return !(*this == other); }

 private:
    void Allocate(std::size_t size);
    void ReleaseNoZero() noexcept;

    std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
    bool locked_ = false;
};

}  // namespace passvault::crypto
