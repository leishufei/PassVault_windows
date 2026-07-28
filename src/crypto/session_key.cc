#include "crypto/session_key.h"

#include <utility>

namespace passvault::crypto {

std::optional<SessionKey> SessionKey::FromSecureBytes(SecureBytes&& bytes) {
    if (bytes.size() != kSize) {
        return std::nullopt;
    }
    return SessionKey(std::move(bytes));
}

}  // namespace passvault::crypto
