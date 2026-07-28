#pragma once

#include <QByteArray>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace passvault::crypto {

class CryptoService {
 public:
    static constexpr std::size_t kKeySize = 32;
    static constexpr std::size_t kIvSize = 12;
    static constexpr std::size_t kTagSize = 16;

    static QByteArray EncryptGcm(
        const std::uint8_t* key,
        std::size_t key_size,
        const std::uint8_t* iv,
        std::size_t iv_size,
        const std::uint8_t* plaintext,
        std::size_t plaintext_size);

    static std::optional<QByteArray> DecryptGcm(
        const std::uint8_t* key,
        std::size_t key_size,
        const std::uint8_t* iv,
        std::size_t iv_size,
        const std::uint8_t* ct_and_tag,
        std::size_t total_size);
};

}  // namespace passvault::crypto
