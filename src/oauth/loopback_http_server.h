#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include <cstdint>

class QTcpServer;
class QTcpSocket;

namespace passvault::oauth {

class LoopbackHttpServer : public QObject {
    Q_OBJECT

 public:
    explicit LoopbackHttpServer(QObject* parent = nullptr);
    ~LoopbackHttpServer() override;

    bool Start(int max_retries = 3);
    void Stop();

    quint16 port() const { return port_; }
    QString redirect_uri() const;

 signals:
    void CallbackReceived(const QHash<QString, QString>& query_params);
    void Error(const QString& message);

 private slots:
    void HandleNewConnection();
    void HandleReadyRead();

 private:
    void ProcessRequest(QTcpSocket* client, const QByteArray& request);
    void SendResponse(QTcpSocket* client, int status_code,
                      const QByteArray& body);
    static QHash<QString, QString> ParseQueryString(const QString& query);
    static QString UrlDecode(const QString& s);

    QTcpServer* server_ = nullptr;
    quint16 port_ = 0;
    QHash<QTcpSocket*, QByteArray> pending_buffers_;
};

}  // namespace passvault::oauth
