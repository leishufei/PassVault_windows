#include "crypto/key_derivation.h"

#include <openssl/evp.h>

#include <stdexcept>

namespace passvault::crypto {

SecureBytes KeyDerivation::Pbkdf2HmacSha256(
    const std::uint8_t* password,
    std::size_t password_len,
    const std::uint8_t* salt,
    std::size_t salt_len,
    int iterations,
    std::size_t key_len) {
    if (key_len == 0 || iterations <= 0) {
        throw std::invalid_argument("PBKDF2 requires positive iterations and key_len");
    }

    SecureBytes out(key_len);
    const int rc = PKCS5_PBKDF2_HMAC(
        reinterpret_cast<const char*>(password),
        static_cast<int>(password_len),
        salt,
        static_cast<int>(salt_len),
        iterations,
        EVP_sha256(),
        static_cast<int>(key_len),
        out.data());
    if (rc != 1) {
        out.Clear();
        throw std::runtime_error("PKCS5_PBKDF2_HMAC failed");
    }
    return out;
}

SecureBytes KeyDerivation::Pbkdf2HmacSha256(
    std::string_view password,
    const std::uint8_t* salt,
    std::size_t salt_len,
    int iterations,
    std::size_t key_len) {
    return Pbkdf2HmacSha256(
        reinterpret_cast<const std::uint8_t*>(password.data()),
        password.size(),
        salt,
        salt_len,
        iterations,
        key_len);
}

}  // namespace passvault::crypto
