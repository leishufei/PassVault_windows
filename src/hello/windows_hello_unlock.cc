#include "hello/windows_hello_unlock.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dpapi.h>
#endif

namespace passvault::hello {

namespace {

std::optional<QByteArray> DpapiProtect(const QByteArray& plaintext) {
#ifdef _WIN32
    DATA_BLOB in{};
    in.pbData =
        reinterpret_cast<BYTE*>(const_cast<char*>(plaintext.constData()));
    in.cbData = static_cast<DWORD>(plaintext.size());

    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"passvault-hello-unlock", nullptr, nullptr,
                          nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return std::nullopt;
    }
    QByteArray result(reinterpret_cast<const char*>(out.pbData),
                      static_cast<int>(out.cbData));
    LocalFree(out.pbData);
    return result;
#else
    Q_UNUSED(plaintext);
    return std::nullopt;
#endif
}

std::optional<QByteArray> DpapiUnprotect(const QByteArray& ciphertext) {
#ifdef _WIN32
    DATA_BLOB in{};
    in.pbData =
        reinterpret_cast<BYTE*>(const_cast<char*>(ciphertext.constData()));
    in.cbData = static_cast<DWORD>(ciphertext.size());

    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return std::nullopt;
    }
    QByteArray result(reinterpret_cast<const char*>(out.pbData),
                      static_cast<int>(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
#else
    Q_UNUSED(ciphertext);
    return std::nullopt;
#endif
}

bool EnsureParentDir(const QString& file_path) {
    const QFileInfo info(file_path);
    const QDir parent = info.dir();
    if (parent.exists()) return true;
    return parent.mkpath(QStringLiteral("."));
}

}  // namespace

WindowsHelloUnlock::WindowsHelloUnlock() : storage_path_(DefaultStoragePath()) {}

WindowsHelloUnlock::WindowsHelloUnlock(QString storage_path)
    : storage_path_(std::move(storage_path)) {}

QString WindowsHelloUnlock::DefaultStoragePath() {
    return QCoreApplication::applicationDirPath() +
           QStringLiteral("/data/hello_unlock.dat");
}

bool WindowsHelloUnlock::IsAvailable() {
    if (!cached_available_.has_value()) {
        cached_available_ = ProbeAvailability();
    }
    if (!*cached_available_) last_error_ = HelloError::kNotAvailable;
    return *cached_available_;
}

bool WindowsHelloUnlock::IsEnrolled() const {
    return QFile::exists(storage_path_);
}

bool WindowsHelloUnlock::Enroll(const QString& master_password) {
    if (!IsAvailable()) {
        last_error_ = HelloError::kNotAvailable;
        return false;
    }
    if (!RequestVerification()) {
        last_error_ = HelloError::kUserCancelled;
        return false;
    }
    QByteArray plain = master_password.toUtf8();
    auto ciphertext = DpapiProtect(plain);
    plain.fill('\0');
    if (!ciphertext.has_value()) {
        last_error_ = HelloError::kDpapiFailure;
        return false;
    }
    if (!EnsureParentDir(storage_path_)) {
        last_error_ = HelloError::kIoError;
        return false;
    }
    QSaveFile out(storage_path_);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        last_error_ = HelloError::kIoError;
        return false;
    }
    if (out.write(*ciphertext) != ciphertext->size()) {
        out.cancelWriting();
        last_error_ = HelloError::kIoError;
        return false;
    }
    if (!out.commit()) {
        last_error_ = HelloError::kIoError;
        return false;
    }
    last_error_ = HelloError::kOk;
    return true;
}

std::optional<QString> WindowsHelloUnlock::Unlock() {
    if (!IsEnrolled()) {
        last_error_ = HelloError::kNotEnrolled;
        return std::nullopt;
    }
    if (!IsAvailable()) {
        last_error_ = HelloError::kNotAvailable;
        return std::nullopt;
    }
    if (!RequestVerification()) {
        last_error_ = HelloError::kUserCancelled;
        return std::nullopt;
    }
    QFile file(storage_path_);
    if (!file.open(QIODevice::ReadOnly)) {
        last_error_ = HelloError::kIoError;
        return std::nullopt;
    }
    const QByteArray blob = file.readAll();
    file.close();
    auto plain = DpapiUnprotect(blob);
    if (!plain.has_value()) {
        last_error_ = HelloError::kDpapiFailure;
        return std::nullopt;
    }
    QString pw = QString::fromUtf8(*plain);
    plain->fill('\0');
    last_error_ = HelloError::kOk;
    return pw;
}

bool WindowsHelloUnlock::Disable() {
    if (!QFile::exists(storage_path_)) {
        last_error_ = HelloError::kOk;
        return true;
    }
    if (QFile::remove(storage_path_)) {
        last_error_ = HelloError::kOk;
        return true;
    }
    last_error_ = HelloError::kIoError;
    return false;
}

}  // namespace passvault::hello
