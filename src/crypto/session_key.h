#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "crypto/secure_bytes.h"

namespace passvault::crypto {

class SessionKey {
 public:
    static constexpr std::size_t kSize = 32;

    SessionKey(const SessionKey&) = delete;
    SessionKey& operator=(const SessionKey&) = delete;

    SessionKey(SessionKey&&) noexcept = default;
    SessionKey& operator=(SessionKey&&) noexcept = default;

    static std::optional<SessionKey> FromSecureBytes(SecureBytes&& bytes);

    const std::uint8_t* data() const noexcept { return bytes_.data(); }
    std::size_t size() const noexcept { return bytes_.size(); }

 private:
    explicit SessionKey(SecureBytes&& bytes) noexcept : bytes_(std::move(bytes)) {}

    SecureBytes bytes_;
};

}  // namespace passvault::crypto
