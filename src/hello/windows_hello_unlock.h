#pragma once

#include <QString>

#include <optional>

namespace passvault::hello {

enum class HelloError {
    kOk,
    kNotAvailable,
    kUserCancelled,
    kDpapiFailure,
    kIoError,
    kNotEnrolled,
    kInternalError,
};

class WindowsHelloUnlock {
 public:
    WindowsHelloUnlock();
    explicit WindowsHelloUnlock(QString storage_path);

    WindowsHelloUnlock(const WindowsHelloUnlock&) = delete;
    WindowsHelloUnlock& operator=(const WindowsHelloUnlock&) = delete;

    bool IsAvailable();

    bool IsEnrolled() const;

    bool Enroll(const QString& master_password);

    std::optional<QString> Unlock();

    bool Disable();

    HelloError last_error() const { return last_error_; }

    const QString& storage_path() const { return storage_path_; }

    void set_availability_for_testing(std::optional<bool> forced) {
        forced_availability_ = forced;
        cached_available_.reset();
    }

    void set_prompt_result_for_testing(std::optional<bool> forced) {
        forced_prompt_ = forced;
    }

    void reset_availability_cache_for_testing() {
        cached_available_.reset();
    }

 private:
    static QString DefaultStoragePath();

    bool RequestVerification();
    bool ProbeAvailability();

    QString storage_path_;
    std::optional<bool> forced_availability_;
    std::optional<bool> forced_prompt_;
    std::optional<bool> cached_available_;
    HelloError last_error_ = HelloError::kOk;
};

}  // namespace passvault::hello
