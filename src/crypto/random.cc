#include "crypto/random.h"

#include <stdexcept>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif
#endif

namespace passvault::crypto {

void Random::Fill(void* buf, std::size_t size) {
    if (buf == nullptr || size == 0) return;
#ifdef _WIN32
    const NTSTATUS status = BCryptGenRandom(
        nullptr,
        static_cast<PUCHAR>(buf),
        static_cast<ULONG>(size),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status != STATUS_SUCCESS) {
        throw std::runtime_error("BCryptGenRandom failed");
    }
#else
#error "Random::Fill only implements Windows backend"
#endif
}

std::vector<std::uint8_t> Random::Bytes(std::size_t size) {
    std::vector<std::uint8_t> out(size);
    if (size > 0) Fill(out.data(), size);
    return out;
}

}  // namespace passvault::crypto
