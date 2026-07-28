#include <gtest/gtest.h>

#include <QByteArray>

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "crypto/crypto_service.h"
#include "crypto/random.h"

namespace {

using passvault::crypto::CryptoService;
using passvault::crypto::Random;

std::vector<std::uint8_t> HexToBytes(std::string_view hex) {
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::vector<std::uint8_t> out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        const int hi = nibble(hex[i]);
        const int lo = nibble(hex[i + 1]);
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return out;
}

std::string BytesToHex(const std::uint8_t* data, std::size_t size) {
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

// NIST SP 800-38D "Test Case 13": AES-256-GCM, empty plaintext,
// key = 32 zero bytes, iv = 12 zero bytes.
TEST(CryptoService, Nist_AesGcm256_EmptyPlaintext) {
    const std::array<std::uint8_t, 32> key{};
    const std::array<std::uint8_t, 12> iv{};

    const QByteArray out = CryptoService::EncryptGcm(
        key.data(), key.size(),
        iv.data(), iv.size(),
        nullptr, 0);
    ASSERT_EQ(out.size(), static_cast<int>(CryptoService::kTagSize));
    EXPECT_EQ(BytesToHex(reinterpret_cast<const std::uint8_t*>(out.constData()),
                         static_cast<std::size_t>(out.size())),
              "530f8afbc74536b9a963b4f1c4cb738b");
}

// NIST SP 800-38D "Test Case 14": AES-256-GCM, 16-byte zero plaintext.
TEST(CryptoService, Nist_AesGcm256_16BytePlaintext) {
    const std::array<std::uint8_t, 32> key{};
    const std::array<std::uint8_t, 12> iv{};
    const std::array<std::uint8_t, 16> pt{};

    const QByteArray out = CryptoService::EncryptGcm(
        key.data(), key.size(),
        iv.data(), iv.size(),
        pt.data(), pt.size());
    ASSERT_EQ(out.size(), static_cast<int>(pt.size() + CryptoService::kTagSize));
    EXPECT_EQ(BytesToHex(reinterpret_cast<const std::uint8_t*>(out.constData()),
                         static_cast<std::size_t>(out.size())),
              "cea7403d4d606b6e074ec5d3baf39d18"
              "d0d1c8a799996bf0265b98b5d48ab919");

    const auto pt_out = CryptoService::DecryptGcm(
        key.data(), key.size(),
        iv.data(), iv.size(),
        reinterpret_cast<const std::uint8_t*>(out.constData()),
        static_cast<std::size_t>(out.size()));
    ASSERT_TRUE(pt_out.has_value());
    EXPECT_EQ(pt_out->size(), static_cast<int>(pt.size()));
    EXPECT_EQ(std::memcmp(pt_out->constData(), pt.data(), pt.size()), 0);
}

TEST(CryptoService, RoundTrip_RandomInputs) {
    const auto key = Random::Bytes(CryptoService::kKeySize);
    const auto iv = Random::Bytes(CryptoService::kIvSize);
    const std::string plaintext = "hello, PassVault interop — 中文 unicode 🔐";
    const auto* pt_ptr = reinterpret_cast<const std::uint8_t*>(plaintext.data());

    const QByteArray ct = CryptoService::EncryptGcm(
        key.data(), key.size(),
        iv.data(), iv.size(),
        pt_ptr, plaintext.size());
    ASSERT_EQ(ct.size(), static_cast<int>(plaintext.size() + CryptoService::kTagSize));

    const auto decrypted = CryptoService::DecryptGcm(
        key.data(), key.size(),
        iv.data(), iv.size(),
        reinterpret_cast<const std::uint8_t*>(ct.constData()),
        static_cast<std::size_t>(ct.size()));
    ASSERT_TRUE(decrypted.has_value());
    EXPECT_EQ(std::string(decrypted->constData(), decrypted->size()), plaintext);
}

TEST(CryptoService, TamperedCiphertext_ReturnsNullopt) {
    const auto key = Random::Bytes(CryptoService::kKeySize);
    const auto iv = Random::Bytes(CryptoService::kIvSize);
    const std::string plaintext = "the quick brown fox";
    QByteArray ct = CryptoService::EncryptGcm(
        key.data(), key.size(),
        iv.data(), iv.size(),
        reinterpret_cast<const std::uint8_t*>(plaintext.data()),
        plaintext.size());
    ASSERT_FALSE(ct.isEmpty());

    ct[0] = static_cast<char>(ct[0] ^ 0x01);
    const auto decrypted = CryptoService::DecryptGcm(
        key.data(), key.size(),
        iv.data(), iv.size(),
        reinterpret_cast<const std::uint8_t*>(ct.constData()),
        static_cast<std::size_t>(ct.size()));
    EXPECT_FALSE(decrypted.has_value());
}

TEST(CryptoService, TamperedTag_ReturnsNullopt) {
    const auto key = Random::Bytes(CryptoService::kKeySize);
    const auto iv = Random::Bytes(CryptoService::kIvSize);
    const std::string plaintext = "authenticate me";
    QByteArray ct = CryptoService::EncryptGcm(
        key.data(), key.size(),
        iv.data(), iv.size(),
        reinterpret_cast<const std::uint8_t*>(plaintext.data()),
        plaintext.size());
    ASSERT_FALSE(ct.isEmpty());

    const int last_index = ct.size() - 1;
    ct[last_index] = static_cast<char>(ct[last_index] ^ 0x80);
    const auto decrypted = CryptoService::DecryptGcm(
        key.data(), key.size(),
        iv.data(), iv.size(),
        reinterpret_cast<const std::uint8_t*>(ct.constData()),
        static_cast<std::size_t>(ct.size()));
    EXPECT_FALSE(decrypted.has_value());
}

TEST(CryptoService, WrongKey_ReturnsNullopt) {
    const auto key = Random::Bytes(CryptoService::kKeySize);
    const auto wrong_key = Random::Bytes(CryptoService::kKeySize);
    const auto iv = Random::Bytes(CryptoService::kIvSize);
    const std::string plaintext = "secret payload";
    const QByteArray ct = CryptoService::EncryptGcm(
        key.data(), key.size(),
        iv.data(), iv.size(),
        reinterpret_cast<const std::uint8_t*>(plaintext.data()),
        plaintext.size());
    ASSERT_FALSE(ct.isEmpty());

    const auto decrypted = CryptoService::DecryptGcm(
        wrong_key.data(), wrong_key.size(),
        iv.data(), iv.size(),
        reinterpret_cast<const std::uint8_t*>(ct.constData()),
        static_cast<std::size_t>(ct.size()));
    EXPECT_FALSE(decrypted.has_value());
}

TEST(CryptoService, ShortCiphertext_ReturnsNullopt) {
    const auto key = Random::Bytes(CryptoService::kKeySize);
    const auto iv = Random::Bytes(CryptoService::kIvSize);
    const std::array<std::uint8_t, 8> too_short{};
    const auto decrypted = CryptoService::DecryptGcm(
        key.data(), key.size(),
        iv.data(), iv.size(),
        too_short.data(), too_short.size());
    EXPECT_FALSE(decrypted.has_value());
}

TEST(CryptoService, InvalidKeyOrIvSize_ReturnsEmptyOrNullopt) {
    const auto key = Random::Bytes(16);  // wrong size
    const auto iv = Random::Bytes(CryptoService::kIvSize);
    const std::array<std::uint8_t, 4> pt{1, 2, 3, 4};

    const QByteArray ct = CryptoService::EncryptGcm(
        key.data(), key.size(),
        iv.data(), iv.size(),
        pt.data(), pt.size());
    EXPECT_TRUE(ct.isEmpty());

    const auto ok_key = Random::Bytes(CryptoService::kKeySize);
    const auto bad_iv = Random::Bytes(8);  // wrong size
    const QByteArray ct2 = CryptoService::EncryptGcm(
        ok_key.data(), ok_key.size(),
        bad_iv.data(), bad_iv.size(),
        pt.data(), pt.size());
    EXPECT_TRUE(ct2.isEmpty());
}
