#include "oauth/loopback_http_server.h"

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

namespace passvault::oauth {

namespace {

constexpr const char* kSuccessHtml =
    "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
    "<title>PassVault</title><style>"
    "body{font-family:system-ui,sans-serif;background:#0e1116;color:#e6edf3;"
    "display:flex;align-items:center;justify-content:center;height:100vh;"
    "margin:0}"
    ".card{max-width:420px;padding:32px;background:#161b22;border-radius:12px;"
    "box-shadow:0 8px 24px rgba(0,0,0,.4);text-align:center}"
    "h1{margin:0 0 12px;font-size:20px}"
    "p{margin:0;color:#8b949e;font-size:14px;line-height:1.6}"
    "</style></head><body><div class=\"card\">"
    "<h1>PassVault 授权成功</h1>"
    "<p>已收到 Google 授权响应。请回到 PassVault 桌面应用继续操作。"
    "此页面可以安全关闭。</p></div></body></html>";

constexpr const char* kErrorHtml =
    "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
    "<title>PassVault</title></head><body>"
    "<h1>请求无效</h1><p>PassVault 只处理 /callback。</p>"
    "</body></html>";

}  // namespace

LoopbackHttpServer::LoopbackHttpServer(QObject* parent) : QObject(parent) {}

LoopbackHttpServer::~LoopbackHttpServer() { Stop(); }

bool LoopbackHttpServer::Start(int max_retries) {
    if (server_ != nullptr) return true;
    server_ = new QTcpServer(this);
    connect(server_, &QTcpServer::newConnection, this,
            &LoopbackHttpServer::HandleNewConnection);
    for (int i = 0; i < max_retries; ++i) {
        if (server_->listen(QHostAddress::LocalHost, 0)) {
            port_ = server_->serverPort();
            return true;
        }
    }
    emit Error(QStringLiteral("Failed to bind loopback port: %1")
                   .arg(server_->errorString()));
    server_->deleteLater();
    server_ = nullptr;
    return false;
}

void LoopbackHttpServer::Stop() {
    if (server_ != nullptr) {
        server_->close();
        server_->deleteLater();
        server_ = nullptr;
    }
    for (auto it = pending_buffers_.begin(); it != pending_buffers_.end();
         ++it) {
        it.key()->deleteLater();
    }
    pending_buffers_.clear();
    port_ = 0;
}

QString LoopbackHttpServer::redirect_uri() const {
    return QStringLiteral("http://127.0.0.1:%1/callback").arg(port_);
}

void LoopbackHttpServer::HandleNewConnection() {
    while (server_ != nullptr && server_->hasPendingConnections()) {
        QTcpSocket* client = server_->nextPendingConnection();
        pending_buffers_.insert(client, {});
        connect(client, &QTcpSocket::readyRead, this,
                &LoopbackHttpServer::HandleReadyRead);
        connect(client, &QTcpSocket::disconnected, client,
                &QTcpSocket::deleteLater);
    }
}

void LoopbackHttpServer::HandleReadyRead() {
    auto* client = qobject_cast<QTcpSocket*>(sender());
    if (client == nullptr) return;
    auto it = pending_buffers_.find(client);
    if (it == pending_buffers_.end()) return;
    it.value().append(client->readAll());
    const int header_end = it.value().indexOf("\r\n\r\n");
    if (header_end < 0) {
        if (it.value().size() > 8192) {
            SendResponse(client, 431, "Request Header Fields Too Large");
            pending_buffers_.remove(client);
        }
        return;
    }
    const QByteArray request = it.value().left(header_end);
    pending_buffers_.remove(client);
    ProcessRequest(client, request);
}

void LoopbackHttpServer::ProcessRequest(QTcpSocket* client,
                                        const QByteArray& request) {
    const QList<QByteArray> lines = request.split('\n');
    if (lines.isEmpty()) {
        SendResponse(client, 400, "Bad Request");
        return;
    }
    const QByteArray start_line = lines.first().trimmed();
    const QList<QByteArray> parts = start_line.split(' ');
    if (parts.size() < 2 || parts[0] != "GET") {
        SendResponse(client, 405, "Method Not Allowed");
        return;
    }
    const QUrl url = QUrl::fromEncoded("http://127.0.0.1" + parts[1]);
    if (url.path() != QStringLiteral("/callback")) {
        SendResponse(client, 404, kErrorHtml);
        return;
    }
    const auto params = ParseQueryString(url.query(QUrl::FullyEncoded));
    SendResponse(client, 200, kSuccessHtml);
    emit CallbackReceived(params);
}

void LoopbackHttpServer::SendResponse(QTcpSocket* client, int status_code,
                                      const QByteArray& body) {
    const QByteArray reason = (status_code == 200)
                                  ? QByteArrayLiteral("OK")
                                  : QByteArrayLiteral("Error");
    QByteArray response;
    response.append("HTTP/1.1 ");
    response.append(QByteArray::number(status_code));
    response.append(' ');
    response.append(reason);
    response.append("\r\n");
    response.append("Content-Type: text/html; charset=utf-8\r\n");
    response.append("Content-Length: ");
    response.append(QByteArray::number(body.size()));
    response.append("\r\n");
    response.append("Connection: close\r\n\r\n");
    response.append(body);
    client->write(response);
    client->flush();
    client->disconnectFromHost();
}

QHash<QString, QString> LoopbackHttpServer::ParseQueryString(
    const QString& query) {
    QHash<QString, QString> out;
    if (query.isEmpty()) return out;
    const QStringList pairs = query.split(QLatin1Char('&'), Qt::SkipEmptyParts);
    for (const QString& pair : pairs) {
        const int eq = pair.indexOf(QLatin1Char('='));
        if (eq < 0) {
            out.insert(UrlDecode(pair), QString());
        } else {
            out.insert(UrlDecode(pair.left(eq)),
                       UrlDecode(pair.mid(eq + 1)));
        }
    }
    return out;
}

QString LoopbackHttpServer::UrlDecode(const QString& s) {
    QByteArray as_utf8 = s.toUtf8();
    as_utf8.replace('+', ' ');
    return QUrl::fromPercentEncoding(as_utf8);
}

}  // namespace passvault::oauth
