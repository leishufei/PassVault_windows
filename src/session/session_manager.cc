#include "session/session_manager.h"

#include <utility>

namespace passvault::session {

SessionManager::SessionManager() = default;

SessionManager::~SessionManager() = default;

SessionManager* SessionManager::Instance() {
    return Storage().get();
}

std::unique_ptr<SessionManager>& SessionManager::Storage() {
    static std::unique_ptr<SessionManager> instance(new SessionManager());
    return instance;
}

void SessionManager::RecreateAfterLocalReset() {
    Storage().reset(new SessionManager());
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

bool SessionManager::BeginPasswordChange() {
    if (password_change_active_ || !unlocked_) return false;
    password_change_active_ = true;
    lock_requested_ = false;
    return true;
}

void SessionManager::EndPasswordChange(bool keep_locked) {
    password_change_active_ = false;
    const bool must_lock = keep_locked || lock_requested_;
    lock_requested_ = false;
    if (must_lock) Lock();
}

void SessionManager::Lock() {
    if (password_change_active_) {
        lock_requested_ = true;
        return;
    }
    if (!unlocked_) return;
    session_key_.reset();
    master_password_.Clear();
    unlocked_ = false;
    emit LockChanged(true);
}

void SessionManager::ResetForTests() {
    password_change_active_ = false;
    lock_requested_ = false;
    session_key_.reset();
    master_password_.Clear();
    unlocked_ = false;
}

}  // namespace passvault::session
