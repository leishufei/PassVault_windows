#include "crypto/crypto_service.h"

#include <openssl/evp.h>

#include <memory>

namespace passvault::crypto {

namespace {

struct EvpCipherCtxDeleter {
    void operator()(EVP_CIPHER_CTX* ctx) const noexcept {
        if (ctx != nullptr) EVP_CIPHER_CTX_free(ctx);
    }
};
using EvpCipherCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, EvpCipherCtxDeleter>;

}  // namespace

QByteArray CryptoService::EncryptGcm(
    const std::uint8_t* key,
    std::size_t key_size,
    const std::uint8_t* iv,
    std::size_t iv_size,
    const std::uint8_t* plaintext,
    std::size_t plaintext_size) {
    if (key == nullptr || iv == nullptr) return QByteArray();
    if (key_size != kKeySize || iv_size != kIvSize) return QByteArray();
    if (plaintext == nullptr && plaintext_size != 0) return QByteArray();

    EvpCipherCtxPtr ctx(EVP_CIPHER_CTX_new());
    if (ctx == nullptr) return QByteArray();

    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
        return QByteArray();
    }
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(kIvSize), nullptr) != 1) {
        return QByteArray();
    }
    if (EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key, iv) != 1) {
        return QByteArray();
    }

    QByteArray out;
    out.resize(static_cast<int>(plaintext_size + kTagSize));
    auto* out_buf = reinterpret_cast<std::uint8_t*>(out.data());

    int written = 0;
    if (plaintext_size > 0) {
        int len = 0;
        if (EVP_EncryptUpdate(ctx.get(), out_buf, &len, plaintext, static_cast<int>(plaintext_size)) != 1) {
            return QByteArray();
        }
        written = len;
    }

    int final_len = 0;
    if (EVP_EncryptFinal_ex(ctx.get(), out_buf + written, &final_len) != 1) {
        return QByteArray();
    }
    written += final_len;

    if (static_cast<std::size_t>(written) != plaintext_size) {
        return QByteArray();
    }

    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, static_cast<int>(kTagSize),
                            out_buf + plaintext_size) != 1) {
        return QByteArray();
    }
    return out;
}

std::optional<QByteArray> CryptoService::DecryptGcm(
    const std::uint8_t* key,
    std::size_t key_size,
    const std::uint8_t* iv,
    std::size_t iv_size,
    const std::uint8_t* ct_and_tag,
    std::size_t total_size) {
    if (key == nullptr || iv == nullptr) return std::nullopt;
    if (key_size != kKeySize || iv_size != kIvSize) return std::nullopt;
    if (total_size < kTagSize) return std::nullopt;
    if (ct_and_tag == nullptr && total_size != 0) return std::nullopt;

    const std::size_t ct_size = total_size - kTagSize;
    const std::uint8_t* tag = ct_and_tag + ct_size;

    EvpCipherCtxPtr ctx(EVP_CIPHER_CTX_new());
    if (ctx == nullptr) return std::nullopt;

    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
        return std::nullopt;
    }
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(kIvSize), nullptr) != 1) {
        return std::nullopt;
    }
    if (EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key, iv) != 1) {
        return std::nullopt;
    }

    QByteArray plaintext;
    plaintext.resize(static_cast<int>(ct_size));
    auto* pt_buf = reinterpret_cast<std::uint8_t*>(plaintext.data());

    int written = 0;
    if (ct_size > 0) {
        int len = 0;
        if (EVP_DecryptUpdate(ctx.get(), pt_buf, &len, ct_and_tag, static_cast<int>(ct_size)) != 1) {
            return std::nullopt;
        }
        written = len;
    }

    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(kTagSize),
                            const_cast<std::uint8_t*>(tag)) != 1) {
        return std::nullopt;
    }

    int final_len = 0;
    if (EVP_DecryptFinal_ex(ctx.get(), pt_buf + written, &final_len) != 1) {
        return std::nullopt;
    }
    written += final_len;

    if (static_cast<std::size_t>(written) != ct_size) {
        return std::nullopt;
    }
    return plaintext;
}

}  // namespace passvault::crypto
