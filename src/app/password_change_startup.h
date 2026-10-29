#pragma once

#include <functional>

#include "master_password/password_change_coordinator.h"

namespace passvault::app {

inline bool UnlockAfterPasswordRecovery(
    master_password::PasswordChangeCoordinator& coordinator,
    sync::CloudStorageProvider* provider,
    const std::function<bool(const QString&)>& retry_recovery,
    const std::function<bool()>& unlock) {
    while (coordinator.NeedsRecovery()) {
        const auto result = coordinator.Recover(provider);
        if (result.outcome == master_password::PasswordChangeCoordinator::Outcome::kLocked &&
            !retry_recovery(result.message)) return false;
    }
    return unlock();
}

}  // namespace passvault::app
