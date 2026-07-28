#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "crypto/key_derivation.h"
#include "crypto/secure_bytes.h"

namespace {

using passvault::crypto::KeyDerivation;
using passvault::crypto::SecureBytes;

std::string ToHexLower(const std::uint8_t* data, std::size_t size) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.resize(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        out[i * 2] = kHex[(data[i] >> 4) & 0x0f];
        out[i * 2 + 1] = kHex[data[i] & 0x0f];
    }
    return out;
}

}  // namespace

// Reference vectors for PBKDF2-HMAC-SHA256 with password="password", salt="salt".
// Widely reproduced across crypto libraries (Python hashlib, Java jasypt tests,
// Rust rust-crypto tests, OpenSSL tests).
TEST(KeyDerivation, Pbkdf2HmacSha256_Iter1) {
    const std::string_view salt = "salt";
    SecureBytes out = KeyDerivation::Pbkdf2HmacSha256(
        "password",
        reinterpret_cast<const std::uint8_t*>(salt.data()), salt.size(),
        1, 32);
    ASSERT_EQ(out.size(), 32u);
    EXPECT_EQ(ToHexLower(out.data(), out.size()),
              "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
}

TEST(KeyDerivation, Pbkdf2HmacSha256_Iter2) {
    const std::string_view salt = "salt";
    SecureBytes out = KeyDerivation::Pbkdf2HmacSha256(
        "password",
        reinterpret_cast<const std::uint8_t*>(salt.data()), salt.size(),
        2, 32);
    EXPECT_EQ(ToHexLower(out.data(), out.size()),
              "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
}

TEST(KeyDerivation, Pbkdf2HmacSha256_Iter4096) {
    const std::string_view salt = "salt";
    SecureBytes out = KeyDerivation::Pbkdf2HmacSha256(
        "password",
        reinterpret_cast<const std::uint8_t*>(salt.data()), salt.size(),
        4096, 32);
    EXPECT_EQ(ToHexLower(out.data(), out.size()),
              "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
}

TEST(KeyDerivation, InvalidArgsThrow) {
    const std::string_view salt = "salt";
    EXPECT_THROW(
        KeyDerivation::Pbkdf2HmacSha256(
            "password",
            reinterpret_cast<const std::uint8_t*>(salt.data()), salt.size(),
            0, 32),
        std::invalid_argument);
    EXPECT_THROW(
        KeyDerivation::Pbkdf2HmacSha256(
            "password",
            reinterpret_cast<const std::uint8_t*>(salt.data()), salt.size(),
            1000, 0),
        std::invalid_argument);
}
