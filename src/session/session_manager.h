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

    bool IsUnlocked() const { return unlocked_; }

    void Unlock(crypto::SessionKey session_key,
                crypto::SecureBytes master_password);

    void Lock();

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
    SessionManager();
    ~SessionManager() override;
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    std::unique_ptr<crypto::SessionKey> session_key_;
    crypto::SecureBytes master_password_;
    bool unlocked_ = false;
};

}  // namespace passvault::session
