#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

namespace passvault::sync {

struct CloudDownloadResult {
    QByteArray data;
    QString version;
};

enum class UploadIfMatchStatus {
    kSuccess,
    kVersionMismatch,
    kError,
};

enum class CloudReadStatus { kFound, kMissing, kError };

struct CloudReadResult {
    CloudReadStatus status = CloudReadStatus::kError;
    CloudDownloadResult backup;
};

class CloudStorageProvider {
 public:
    virtual ~CloudStorageProvider() = default;

    virtual QString ProviderName() const = 0;

    // Account identifier for display (email address, etc). Empty when unknown;
    // callers fall back to ProviderName(). May perform a blocking lookup.
    virtual QString AccountHint() { return QString(); }

    virtual bool IsAuthenticated() const = 0;
    virtual void SignOut() = 0;

    virtual bool UploadBackup(const QByteArray& data,
                              const QString& remote_name,
                              QString* out_error) = 0;

    virtual std::optional<QByteArray> DownloadBackup(
        const QString& remote_name, QString* out_error) = 0;

    virtual std::optional<CloudDownloadResult> DownloadBackupWithVersion(
        const QString& remote_name, QString* out_error) = 0;

    // Legacy providers cannot assert that a failed download means missing.
    virtual CloudReadResult ReadBackup(const QString& remote_name,
                                       QString* out_error) {
        auto result = DownloadBackupWithVersion(remote_name, out_error);
        if (!result) return {};
        return {CloudReadStatus::kFound, std::move(*result)};
    }

    virtual UploadIfMatchStatus UploadBackupIfMatch(
        const QByteArray& data, const QString& remote_name,
        const QString& expected_version, QString* out_error) = 0;
};

}  // namespace passvault::sync
