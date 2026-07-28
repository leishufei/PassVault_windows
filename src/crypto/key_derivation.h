#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "crypto/secure_bytes.h"

namespace passvault::crypto {

class KeyDerivation {
 public:
    static constexpr int kDefaultIterations = 600000;
    static constexpr std::size_t kDefaultKeyLen = 32;
    static constexpr std::size_t kDefaultSaltLen = 16;

    static SecureBytes Pbkdf2HmacSha256(
        const std::uint8_t* password,
        std::size_t password_len,
        const std::uint8_t* salt,
        std::size_t salt_len,
        int iterations,
        std::size_t key_len);

    static SecureBytes Pbkdf2HmacSha256(
        std::string_view password,
        const std::uint8_t* salt,
        std::size_t salt_len,
        int iterations = kDefaultIterations,
        std::size_t key_len = kDefaultKeyLen);
};

}  // namespace passvault::crypto
