#include "sync/google_drive_provider.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

namespace passvault::sync {

namespace {

QByteArray BuildBoundary() {
    return QByteArrayLiteral("passvault_drive_boundary_9c2f");
}

QString EscapeSingleQuotes(const QString& s) {
    QString out = s;
    out.replace(QChar('\''), QStringLiteral("\\'"));
    return out;
}

}  // namespace

GoogleDriveProvider::GoogleDriveProvider(AccessTokenFetcher token_fetcher,
                                         QObject* parent)
    : QObject(parent), token_fetcher_(std::move(token_fetcher)) {}

GoogleDriveProvider::~GoogleDriveProvider() {
    if (owns_network_ && network_) {
        delete network_;
        network_ = nullptr;
    }
}

void GoogleDriveProvider::set_network_manager(QNetworkAccessManager* mgr) {
    if (owns_network_ && network_) {
        delete network_;
    }
    network_ = mgr;
    owns_network_ = false;
}

void GoogleDriveProvider::EnsureNetwork() {
    if (network_) return;
    network_ = new QNetworkAccessManager(this);
    owns_network_ = false;  // parented to `this`, deleted by Qt
}

bool GoogleDriveProvider::IsAuthenticated() const {
    return token_fetcher_ && !token_fetcher_().isEmpty();
}

void GoogleDriveProvider::SignOut() {
    // Tokens are held by GoogleOAuthClient; this provider has no credentials
    // of its own to clear. Callers should invoke
    // GoogleOAuthClient::RevokeTokens directly to revoke access.
}

GoogleDriveProvider::HttpResult GoogleDriveProvider::SendRequestSync(
    const QByteArray& verb, const QString& url, const QByteArray& body,
    const QByteArray& content_type) {
    HttpResult result;
    if (!token_fetcher_) {
        result.error = QStringLiteral("No access token fetcher configured");
        return result;
    }
    const QString token = token_fetcher_();
    if (token.isEmpty()) {
        result.error = QStringLiteral("Not authenticated");
        return result;
    }
    EnsureNetwork();

    QNetworkRequest request{QUrl(url)};
    request.setRawHeader("Authorization",
                         QByteArrayLiteral("Bearer ") + token.toUtf8());
    if (!content_type.isEmpty()) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, content_type);
    }

    QNetworkReply* reply = nullptr;
    if (verb == "GET") {
        reply = network_->get(request);
    } else if (verb == "DELETE") {
        reply = network_->deleteResource(request);
    } else {
        reply = network_->sendCustomRequest(request, verb, body);
    }

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    result.http_status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError && !result.ok()) {
        result.error =
            QStringLiteral("HTTP %1: %2").arg(result.http_status).arg(reply->errorString());
    } else if (result.http_status < 200 || result.http_status >= 300) {
        result.error =
            QStringLiteral("HTTP %1: %2")
                .arg(result.http_status)
                .arg(QString::fromUtf8(result.body.left(512)));
    }
    reply->deleteLater();
    return result;
}

GoogleDriveProvider::HttpResult GoogleDriveProvider::SendMultipartUploadSync(
    const QByteArray& verb, const QString& url,
    const QByteArray& metadata_json, const QByteArray& file_data) {
    const QByteArray boundary = BuildBoundary();
    QByteArray body;
    body.reserve(metadata_json.size() + file_data.size() + boundary.size() * 3 + 256);
    body.append("--").append(boundary).append("\r\n");
    body.append("Content-Type: application/json; charset=UTF-8\r\n\r\n");
    body.append(metadata_json).append("\r\n");
    body.append("--").append(boundary).append("\r\n");
    body.append("Content-Type: application/json\r\n\r\n");
    body.append(file_data).append("\r\n");
    body.append("--").append(boundary).append("--");

    const QByteArray content_type =
        QByteArrayLiteral("multipart/related; boundary=") + boundary;
    return SendRequestSync(verb, url, body, content_type);
}

std::optional<QString> GoogleDriveProvider::GetOrCreateFolder(QString* out_error) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("spaces"), QStringLiteral("drive"));
    const QString q = QStringLiteral(
                          "name = '%1' and mimeType = "
                          "'application/vnd.google-apps.folder' and "
                          "trashed = false")
                          .arg(EscapeSingleQuotes(backup_folder_name_));
    query.addQueryItem(QStringLiteral("q"), q);
    query.addQueryItem(QStringLiteral("fields"),
                       QStringLiteral("files(id,name)"));
    query.addQueryItem(QStringLiteral("pageSize"), QStringLiteral("1"));

    QUrl list_url(base_url_ + QStringLiteral("/files"));
    list_url.setQuery(query);

    HttpResult list_result =
        SendRequestSync("GET", list_url.toString(QUrl::FullyEncoded), {}, {});
    if (!list_result.ok()) {
        if (out_error) *out_error = list_result.error;
        return std::nullopt;
    }

    QJsonParseError parse_err{};
    const QJsonDocument doc = QJsonDocument::fromJson(list_result.body, &parse_err);
    if (parse_err.error != QJsonParseError::NoError || !doc.isObject()) {
        if (out_error) *out_error = QStringLiteral("Malformed folder list response");
        return std::nullopt;
    }
    const QJsonArray files = doc.object().value(QStringLiteral("files")).toArray();
    if (!files.isEmpty()) {
        const QString id = files.at(0).toObject().value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) return id;
    }

    // Create folder.
    QJsonObject meta;
    meta.insert(QStringLiteral("name"), backup_folder_name_);
    meta.insert(QStringLiteral("mimeType"),
                QStringLiteral("application/vnd.google-apps.folder"));

    QUrl create_url(base_url_ + QStringLiteral("/files"));
    QUrlQuery create_query;
    create_query.addQueryItem(QStringLiteral("fields"), QStringLiteral("id"));
    create_url.setQuery(create_query);

    HttpResult create_result = SendRequestSync(
        "POST", create_url.toString(QUrl::FullyEncoded),
        QJsonDocument(meta).toJson(QJsonDocument::Compact),
        QByteArrayLiteral("application/json"));
    if (!create_result.ok()) {
        if (out_error) *out_error = create_result.error;
        return std::nullopt;
    }
    const QJsonDocument create_doc = QJsonDocument::fromJson(create_result.body);
    const QString id = create_doc.object().value(QStringLiteral("id")).toString();
    if (id.isEmpty()) {
        if (out_error) *out_error = QStringLiteral("Folder create response missing id");
        return std::nullopt;
    }
    return id;
}

std::optional<QString> GoogleDriveProvider::FindFileId(const QString& file_name,
                                                       const QString& folder_id,
                                                       QString* out_error) {
    const QString q = QStringLiteral(
                          "name = '%1' and '%2' in parents and trashed = false")
                          .arg(EscapeSingleQuotes(file_name),
                               EscapeSingleQuotes(folder_id));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("spaces"), QStringLiteral("drive"));
    query.addQueryItem(QStringLiteral("q"), q);
    query.addQueryItem(QStringLiteral("fields"),
                       QStringLiteral("files(id,name)"));
    query.addQueryItem(QStringLiteral("pageSize"), QStringLiteral("1"));

    QUrl url(base_url_ + QStringLiteral("/files"));
    url.setQuery(query);

    HttpResult result = SendRequestSync("GET", url.toString(QUrl::FullyEncoded), {}, {});
    if (!result.ok()) {
        if (out_error) *out_error = result.error;
        return std::nullopt;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(result.body);
    const QJsonArray files = doc.object().value(QStringLiteral("files")).toArray();
    if (files.isEmpty()) {
        return QString{};  // File not found (empty string signals "not present")
    }
    return files.at(0).toObject().value(QStringLiteral("id")).toString();
}

std::optional<QString> GoogleDriveProvider::GetFileModifiedTime(
    const QString& file_id, QString* out_error) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("modifiedTime"));

    QUrl url(base_url_ + QStringLiteral("/files/") + file_id);
    url.setQuery(query);

    HttpResult result = SendRequestSync("GET", url.toString(QUrl::FullyEncoded), {}, {});
    if (!result.ok()) {
        if (out_error) *out_error = result.error;
        return std::nullopt;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(result.body);
    const QString mt = doc.object().value(QStringLiteral("modifiedTime")).toString();
    if (mt.isEmpty()) {
        if (out_error) *out_error = QStringLiteral("modifiedTime missing");
        return std::nullopt;
    }
    return mt;
}

std::optional<QByteArray> GoogleDriveProvider::DownloadFileMedia(
    const QString& file_id, QString* out_error) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("alt"), QStringLiteral("media"));

    QUrl url(base_url_ + QStringLiteral("/files/") + file_id);
    url.setQuery(query);

    HttpResult result = SendRequestSync("GET", url.toString(QUrl::FullyEncoded), {}, {});
    if (!result.ok()) {
        if (out_error) *out_error = result.error;
        return std::nullopt;
    }
    return result.body;
}

bool GoogleDriveProvider::UpdateFile(const QString& file_id,
                                     const QByteArray& data,
                                     QString* out_error) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("uploadType"), QStringLiteral("media"));

    QUrl url(upload_base_url_ + QStringLiteral("/files/") + file_id);
    url.setQuery(query);

    HttpResult result = SendRequestSync("PATCH", url.toString(QUrl::FullyEncoded),
                                        data, QByteArrayLiteral("application/json"));
    if (!result.ok()) {
        if (out_error) *out_error = result.error;
        return false;
    }
    return true;
}

bool GoogleDriveProvider::CreateFile(const QString& file_name,
                                     const QString& folder_id,
                                     const QByteArray& data,
                                     QString* out_error) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("uploadType"), QStringLiteral("multipart"));
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("id"));

    QUrl url(upload_base_url_ + QStringLiteral("/files"));
    url.setQuery(query);

    QJsonObject meta;
    meta.insert(QStringLiteral("name"), file_name);
    QJsonArray parents;
    parents.append(folder_id);
    meta.insert(QStringLiteral("parents"), parents);

    HttpResult result = SendMultipartUploadSync(
        "POST", url.toString(QUrl::FullyEncoded),
        QJsonDocument(meta).toJson(QJsonDocument::Compact), data);
    if (!result.ok()) {
        if (out_error) *out_error = result.error;
        return false;
    }
    return true;
}

bool GoogleDriveProvider::UploadBackup(const QByteArray& data,
                                       const QString& remote_name,
                                       QString* out_error) {
    auto folder_id = GetOrCreateFolder(out_error);
    if (!folder_id) return false;

    auto file_id = FindFileId(remote_name, *folder_id, out_error);
    if (!file_id) return false;
    if (file_id->isEmpty()) {
        return CreateFile(remote_name, *folder_id, data, out_error);
    }
    return UpdateFile(*file_id, data, out_error);
}

std::optional<QByteArray> GoogleDriveProvider::DownloadBackup(
    const QString& remote_name, QString* out_error) {
    auto folder_id = GetOrCreateFolder(out_error);
    if (!folder_id) return std::nullopt;

    auto file_id = FindFileId(remote_name, *folder_id, out_error);
    if (!file_id) return std::nullopt;
    if (file_id->isEmpty()) {
        if (out_error) *out_error = QStringLiteral("Backup file not found on Drive");
        return std::nullopt;
    }
    return DownloadFileMedia(*file_id, out_error);
}

std::optional<CloudDownloadResult> GoogleDriveProvider::DownloadBackupWithVersion(
    const QString& remote_name, QString* out_error) {
    auto folder_id = GetOrCreateFolder(out_error);
    if (!folder_id) return std::nullopt;

    auto file_id = FindFileId(remote_name, *folder_id, out_error);
    if (!file_id) return std::nullopt;
    if (file_id->isEmpty()) {
        if (out_error) *out_error = QStringLiteral("Backup file not found on Drive");
        return std::nullopt;
    }

    auto modified_time = GetFileModifiedTime(*file_id, out_error);
    if (!modified_time) return std::nullopt;

    auto body = DownloadFileMedia(*file_id, out_error);
    if (!body) return std::nullopt;

    CloudDownloadResult out;
    out.data = *body;
    out.version = *modified_time;
    return out;
}

UploadIfMatchStatus GoogleDriveProvider::UploadBackupIfMatch(
    const QByteArray& data, const QString& remote_name,
    const QString& expected_version, QString* out_error) {
    auto folder_id = GetOrCreateFolder(out_error);
    if (!folder_id) return UploadIfMatchStatus::kError;

    auto file_id = FindFileId(remote_name, *folder_id, out_error);
    if (!file_id) return UploadIfMatchStatus::kError;

    if (file_id->isEmpty()) {
        if (CreateFile(remote_name, *folder_id, data, out_error)) {
            return UploadIfMatchStatus::kSuccess;
        }
        return UploadIfMatchStatus::kError;
    }

    auto current_version = GetFileModifiedTime(*file_id, out_error);
    if (!current_version) return UploadIfMatchStatus::kError;

    if (*current_version != expected_version) {
        if (out_error) {
            *out_error =
                QStringLiteral("Cloud file modified elsewhere (expected=%1, actual=%2)")
                    .arg(expected_version, *current_version);
        }
        return UploadIfMatchStatus::kVersionMismatch;
    }

    if (UpdateFile(*file_id, data, out_error)) {
        return UploadIfMatchStatus::kSuccess;
    }
    return UploadIfMatchStatus::kError;
}

}  // namespace passvault::sync
