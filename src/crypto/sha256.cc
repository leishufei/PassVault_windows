#include "crypto/sha256.h"

#include <openssl/evp.h>

#include <array>
#include <stdexcept>

namespace passvault::crypto {

namespace {

constexpr char kHexLower[] = "0123456789abcdef";

}  // namespace

std::vector<std::uint8_t> Sha256(const std::uint8_t* data, std::size_t size) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (ctx == nullptr) throw std::runtime_error("EVP_MD_CTX_new failed");

    std::vector<std::uint8_t> out(32);
    unsigned int out_len = 0;

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1 ||
        (size > 0 && EVP_DigestUpdate(ctx, data, size) != 1) ||
        EVP_DigestFinal_ex(ctx, out.data(), &out_len) != 1 ||
        out_len != 32) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("SHA-256 digest failed");
    }
    EVP_MD_CTX_free(ctx);
    return out;
}

std::vector<std::uint8_t> Sha256(std::string_view s) {
    return Sha256(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}

std::string Sha256HexLower(const std::uint8_t* data, std::size_t size) {
    const auto digest = Sha256(data, size);
    std::string out;
    out.resize(digest.size() * 2);
    for (std::size_t i = 0; i < digest.size(); ++i) {
        out[i * 2] = kHexLower[(digest[i] >> 4) & 0x0f];
        out[i * 2 + 1] = kHexLower[digest[i] & 0x0f];
    }
    return out;
}

std::string Sha256HexLower(std::string_view s) {
    return Sha256HexLower(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}

}  // namespace passvault::crypto
