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

class CloudStorageProvider {
 public:
    virtual ~CloudStorageProvider() = default;

    virtual QString ProviderName() const = 0;
    virtual bool IsAuthenticated() const = 0;
    virtual void SignOut() = 0;

    virtual bool UploadBackup(const QByteArray& data,
                              const QString& remote_name,
                              QString* out_error) = 0;

    virtual std::optional<QByteArray> DownloadBackup(
        const QString& remote_name, QString* out_error) = 0;

    virtual std::optional<CloudDownloadResult> DownloadBackupWithVersion(
        const QString& remote_name, QString* out_error) = 0;

    virtual UploadIfMatchStatus UploadBackupIfMatch(
        const QByteArray& data, const QString& remote_name,
        const QString& expected_version, QString* out_error) = 0;
};

}  // namespace passvault::sync
