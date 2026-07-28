#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryDir>

#include "oauth/token_store.h"

using passvault::oauth::TokenStore;

namespace {

QString TempTokenPath(const QTemporaryDir& dir) {
    return dir.path() + QStringLiteral("/google_token.dat");
}

}  // namespace

TEST(TokenStore, SaveAndLoadRoundTrip) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    TokenStore store(TempTokenPath(dir));
    const QString token = QStringLiteral("1//0e-refresh-token-example_12345");
    ASSERT_TRUE(store.SaveRefreshToken(token));
    EXPECT_TRUE(store.Exists());
    const auto loaded = store.LoadRefreshToken();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(*loaded, token);
}

TEST(TokenStore, LoadReturnsNulloptWhenFileMissing) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    TokenStore store(TempTokenPath(dir));
    EXPECT_FALSE(store.Exists());
    EXPECT_FALSE(store.LoadRefreshToken().has_value());
}

TEST(TokenStore, ClearRemovesFile) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    TokenStore store(TempTokenPath(dir));
    ASSERT_TRUE(store.SaveRefreshToken(QStringLiteral("t")));
    ASSERT_TRUE(store.Exists());
    ASSERT_TRUE(store.Clear());
    EXPECT_FALSE(store.Exists());
}

TEST(TokenStore, RejectsEmptyToken) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    TokenStore store(TempTokenPath(dir));
    EXPECT_FALSE(store.SaveRefreshToken(QString()));
    EXPECT_FALSE(store.Exists());
}

TEST(TokenStore, DoesNotStorePlaintextTokenOnDisk) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    TokenStore store(TempTokenPath(dir));
    const QString marker = QStringLiteral("plaintext-marker-not-encrypted");
    ASSERT_TRUE(store.SaveRefreshToken(marker));
    QFile file(store.file_path());
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray raw = file.readAll();
    file.close();
    EXPECT_FALSE(raw.contains(marker.toUtf8()));
}

TEST(TokenStore, ClearIsIdempotent) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    TokenStore store(TempTokenPath(dir));
    EXPECT_TRUE(store.Clear());
    EXPECT_TRUE(store.Clear());
}
