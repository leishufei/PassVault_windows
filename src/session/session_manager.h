#pragma once

#include <QObject>

#include <memory>

#include "crypto/secure_bytes.h"
#include "crypto/session_key.h"

namespace passvault::session {

class SessionManager : public QObject {
    Q_OBJECT

 public:
    static SessionManager* Instance();
    static void RecreateAfterLocalReset();

    bool IsUnlocked() const { return unlocked_; }

    void Unlock(crypto::SessionKey session_key,
                crypto::SecureBytes master_password);

    void Lock();
    bool BeginPasswordChange();
    void EndPasswordChange(bool keep_locked);
    bool password_change_active() const { return password_change_active_; }
    bool lock_requested() const { return lock_requested_; }

    const crypto::SessionKey* session_key() const {
        return unlocked_ ? session_key_.get() : nullptr;
    }
    const crypto::SecureBytes* master_password() const {
        return unlocked_ ? &master_password_ : nullptr;
    }

    void ResetForTests();

 signals:
    void LockChanged(bool locked);

 private:
    friend struct std::default_delete<SessionManager>;
    SessionManager();
    ~SessionManager() override;
    static std::unique_ptr<SessionManager>& Storage();
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    std::unique_ptr<crypto::SessionKey> session_key_;
    crypto::SecureBytes master_password_;
    bool unlocked_ = false;
    bool password_change_active_ = false;
    bool lock_requested_ = false;
};

}  // namespace passvault::session
