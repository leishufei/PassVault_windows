#include "session/session_manager.h"

#include <utility>

namespace passvault::session {

SessionManager::SessionManager() = default;

SessionManager::~SessionManager() = default;

SessionManager* SessionManager::Instance() {
    static SessionManager instance;
    return &instance;
}

void SessionManager::Unlock(crypto::SessionKey session_key,
                            crypto::SecureBytes master_password) {
    session_key_ =
        std::make_unique<crypto::SessionKey>(std::move(session_key));
    master_password_ = std::move(master_password);
    const bool was_locked = !unlocked_;
    unlocked_ = true;
    if (was_locked) emit LockChanged(false);
}

void SessionManager::Lock() {
    if (!unlocked_) return;
    session_key_.reset();
    master_password_.Clear();
    unlocked_ = false;
    emit LockChanged(true);
}

void SessionManager::ResetForTests() {
    session_key_.reset();
    master_password_.Clear();
    unlocked_ = false;
}

}  // namespace passvault::session
