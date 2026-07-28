#include "oauth/token_store.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include "crypto/sha256.h"

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

namespace passvault::oauth {

namespace {

#ifdef _WIN32
QByteArray ReadMachineGuid() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Cryptography", 0,
                      KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
        return {};
    }
    wchar_t buf[128] = {};
    DWORD buf_size = sizeof(buf);
    DWORD type = 0;
    const LONG rc = RegQueryValueExW(key, L"MachineGuid", nullptr, &type,
                                     reinterpret_cast<LPBYTE>(buf), &buf_size);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || type != REG_SZ) return {};
    return QString::fromWCharArray(buf).toUtf8();
}
#endif

QByteArray EntropyBytes() {
#ifdef _WIN32
    const QByteArray guid = ReadMachineGuid();
    const QByteArray material = QByteArrayLiteral("PassVault:") + guid;
    const auto digest =
        crypto::Sha256(reinterpret_cast<const std::uint8_t*>(material.constData()),
                       static_cast<std::size_t>(material.size()));
    return QByteArray(reinterpret_cast<const char*>(digest.data()),
                      static_cast<int>(digest.size()));
#else
    return {};
#endif
}

std::optional<QByteArray> ProtectData(const QByteArray& plaintext) {
#ifdef _WIN32
    QByteArray entropy = EntropyBytes();
    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plaintext.constData()));
    in.cbData = static_cast<DWORD>(plaintext.size());

    DATA_BLOB entropy_blob{};
    entropy_blob.pbData =
        reinterpret_cast<BYTE*>(const_cast<char*>(entropy.constData()));
    entropy_blob.cbData = static_cast<DWORD>(entropy.size());

    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"passvault-google-token",
                          entropy.isEmpty() ? nullptr : &entropy_blob, nullptr,
                          nullptr, 0, &out)) {
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

std::optional<QByteArray> UnprotectData(const QByteArray& ciphertext) {
#ifdef _WIN32
    QByteArray entropy = EntropyBytes();
    DATA_BLOB in{};
    in.pbData =
        reinterpret_cast<BYTE*>(const_cast<char*>(ciphertext.constData()));
    in.cbData = static_cast<DWORD>(ciphertext.size());

    DATA_BLOB entropy_blob{};
    entropy_blob.pbData =
        reinterpret_cast<BYTE*>(const_cast<char*>(entropy.constData()));
    entropy_blob.cbData = static_cast<DWORD>(entropy.size());

    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr,
                            entropy.isEmpty() ? nullptr : &entropy_blob,
                            nullptr, nullptr, 0, &out)) {
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

TokenStore::TokenStore() : file_path_(DefaultFilePath()) {}

TokenStore::TokenStore(QString file_path) : file_path_(std::move(file_path)) {}

QString TokenStore::DefaultFilePath() {
    const QString app_data =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return app_data + QStringLiteral("/google_token.dat");
}

bool TokenStore::Exists() const { return QFile::exists(file_path_); }

bool TokenStore::SaveRefreshToken(const QString& refresh_token) const {
    if (refresh_token.isEmpty()) return false;
    if (!EnsureParentDir(file_path_)) return false;

    QByteArray plaintext = refresh_token.toUtf8();
    auto ciphertext = ProtectData(plaintext);
    plaintext.fill('\0');
    if (!ciphertext.has_value()) return false;

    QSaveFile out(file_path_);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    if (out.write(*ciphertext) != ciphertext->size()) {
        out.cancelWriting();
        return false;
    }
    return out.commit();
}

std::optional<QString> TokenStore::LoadRefreshToken() const {
    QFile file(file_path_);
    if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
    const QByteArray blob = file.readAll();
    file.close();

    auto plaintext = UnprotectData(blob);
    if (!plaintext.has_value()) return std::nullopt;
    QString token = QString::fromUtf8(*plaintext);
    plaintext->fill('\0');
    if (token.isEmpty()) return std::nullopt;
    return token;
}

bool TokenStore::Clear() const {
    if (!QFile::exists(file_path_)) return true;
    return QFile::remove(file_path_);
}

}  // namespace passvault::oauth
