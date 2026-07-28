#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

#include "sync/cloud_storage_provider.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace passvault::sync {

class GoogleDriveProvider : public QObject, public CloudStorageProvider {
    Q_OBJECT

 public:
    using AccessTokenFetcher = std::function<QString()>;

    explicit GoogleDriveProvider(AccessTokenFetcher token_fetcher,
                                 QObject* parent = nullptr);
    ~GoogleDriveProvider() override;

    void set_network_manager(QNetworkAccessManager* mgr);

    void set_backup_folder_name(QString name) {
        backup_folder_name_ = std::move(name);
    }
    void set_base_url(QString url) { base_url_ = std::move(url); }
    void set_upload_base_url(QString url) { upload_base_url_ = std::move(url); }

    QString ProviderName() const override { return QStringLiteral("Google Drive"); }
    bool IsAuthenticated() const override;
    void SignOut() override;

    bool UploadBackup(const QByteArray& data, const QString& remote_name,
                      QString* out_error) override;

    std::optional<QByteArray> DownloadBackup(const QString& remote_name,
                                             QString* out_error) override;

    std::optional<CloudDownloadResult> DownloadBackupWithVersion(
        const QString& remote_name, QString* out_error) override;

    UploadIfMatchStatus UploadBackupIfMatch(
        const QByteArray& data, const QString& remote_name,
        const QString& expected_version, QString* out_error) override;

 private:
    struct HttpResult {
        int http_status = 0;
        QByteArray body;
        QString error;
        bool ok() const { return error.isEmpty() && http_status >= 200 && http_status < 300; }
    };

    void EnsureNetwork();
    HttpResult SendRequestSync(const QByteArray& verb, const QString& url,
                               const QByteArray& body, const QByteArray& content_type);
    HttpResult SendMultipartUploadSync(const QByteArray& verb, const QString& url,
                                       const QByteArray& metadata_json,
                                       const QByteArray& file_data);

    std::optional<QString> GetOrCreateFolder(QString* out_error);
    std::optional<QString> FindFileId(const QString& file_name,
                                      const QString& folder_id,
                                      QString* out_error);
    std::optional<QString> GetFileModifiedTime(const QString& file_id,
                                               QString* out_error);
    std::optional<QByteArray> DownloadFileMedia(const QString& file_id,
                                                QString* out_error);
    bool UpdateFile(const QString& file_id, const QByteArray& data,
                    QString* out_error);
    bool CreateFile(const QString& file_name, const QString& folder_id,
                    const QByteArray& data, QString* out_error);

    AccessTokenFetcher token_fetcher_;
    QNetworkAccessManager* network_ = nullptr;
    bool owns_network_ = false;

    QString backup_folder_name_ = QStringLiteral("PassVault");
    QString base_url_ = QStringLiteral("https://www.googleapis.com/drive/v3");
    QString upload_base_url_ =
        QStringLiteral("https://www.googleapis.com/upload/drive/v3");
};

}  // namespace passvault::sync
