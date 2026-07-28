#include <gtest/gtest.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QHash>
#include <QHostAddress>
#include <QSignalSpy>
#include <QString>
#include <QTcpSocket>

#include "oauth/loopback_http_server.h"

using passvault::oauth::LoopbackHttpServer;

namespace {

bool WaitBytesWritten(QTcpSocket& sock, int timeout_ms = 2000) {
    return sock.waitForBytesWritten(timeout_ms);
}

QHash<QString, QString> FirstEmittedParams(QSignalSpy& spy) {
    if (spy.isEmpty()) return {};
    return spy.first().at(0).value<QHash<QString, QString>>();
}

}  // namespace

TEST(LoopbackHttpServer, StartsOnLocalHostWithNonZeroPort) {
    LoopbackHttpServer server;
    ASSERT_TRUE(server.Start());
    EXPECT_GT(server.port(), 0);
    EXPECT_TRUE(
        server.redirect_uri().startsWith(QStringLiteral("http://127.0.0.1:")));
    EXPECT_TRUE(server.redirect_uri().endsWith(QStringLiteral("/callback")));
}

TEST(LoopbackHttpServer, EmitsCallbackForValidGet) {
    qRegisterMetaType<QHash<QString, QString>>("QHash<QString,QString>");
    LoopbackHttpServer server;
    ASSERT_TRUE(server.Start());
    QSignalSpy spy(&server, &LoopbackHttpServer::CallbackReceived);

    QTcpSocket sock;
    sock.connectToHost(QHostAddress::LocalHost, server.port());
    ASSERT_TRUE(sock.waitForConnected(2000));
    const QByteArray req =
        "GET /callback?code=abc123&state=xyz HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n\r\n";
    sock.write(req);
    ASSERT_TRUE(WaitBytesWritten(sock));

    ASSERT_TRUE(spy.wait(2000));
    ASSERT_EQ(spy.size(), 1);
    const auto params = FirstEmittedParams(spy);
    EXPECT_EQ(params.value(QStringLiteral("code")), QStringLiteral("abc123"));
    EXPECT_EQ(params.value(QStringLiteral("state")), QStringLiteral("xyz"));

    sock.waitForReadyRead(2000);
    const QByteArray resp = sock.readAll();
    EXPECT_TRUE(resp.startsWith("HTTP/1.1 200"));
}

TEST(LoopbackHttpServer, ReturnsErrorForWrongPath) {
    qRegisterMetaType<QHash<QString, QString>>("QHash<QString,QString>");
    LoopbackHttpServer server;
    ASSERT_TRUE(server.Start());
    QSignalSpy spy(&server, &LoopbackHttpServer::CallbackReceived);

    QTcpSocket sock;
    QSignalSpy disc_spy(&sock, &QTcpSocket::disconnected);
    sock.connectToHost(QHostAddress::LocalHost, server.port());
    ASSERT_TRUE(sock.waitForConnected(2000));
    sock.write(
        "GET /other HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n\r\n");
    ASSERT_TRUE(WaitBytesWritten(sock));

    ASSERT_TRUE(disc_spy.wait(2000));
    const QByteArray resp = sock.readAll();
    EXPECT_TRUE(resp.startsWith("HTTP/1.1 404"));
    EXPECT_TRUE(spy.isEmpty());
}

TEST(LoopbackHttpServer, ReturnsMethodNotAllowedForPost) {
    LoopbackHttpServer server;
    ASSERT_TRUE(server.Start());
    QTcpSocket sock;
    QSignalSpy disc_spy(&sock, &QTcpSocket::disconnected);
    sock.connectToHost(QHostAddress::LocalHost, server.port());
    ASSERT_TRUE(sock.waitForConnected(2000));
    sock.write(
        "POST /callback HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Length: 0\r\n\r\n");
    ASSERT_TRUE(WaitBytesWritten(sock));
    ASSERT_TRUE(disc_spy.wait(2000));
    const QByteArray resp = sock.readAll();
    EXPECT_TRUE(resp.startsWith("HTTP/1.1 405"));
}

TEST(LoopbackHttpServer, DecodesPercentEncodedParams) {
    qRegisterMetaType<QHash<QString, QString>>("QHash<QString,QString>");
    LoopbackHttpServer server;
    ASSERT_TRUE(server.Start());
    QSignalSpy spy(&server, &LoopbackHttpServer::CallbackReceived);

    QTcpSocket sock;
    sock.connectToHost(QHostAddress::LocalHost, server.port());
    ASSERT_TRUE(sock.waitForConnected(2000));
    sock.write(
        "GET /callback?code=a%20b&state=hello%2Fworld HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n\r\n");
    ASSERT_TRUE(WaitBytesWritten(sock));

    ASSERT_TRUE(spy.wait(2000));
    const auto params = FirstEmittedParams(spy);
    EXPECT_EQ(params.value(QStringLiteral("code")), QStringLiteral("a b"));
    EXPECT_EQ(params.value(QStringLiteral("state")),
              QStringLiteral("hello/world"));
}

TEST(LoopbackHttpServer, StopReleasesPort) {
    LoopbackHttpServer server;
    ASSERT_TRUE(server.Start());
    EXPECT_GT(server.port(), 0);
    server.Stop();
    EXPECT_EQ(server.port(), 0);
}
