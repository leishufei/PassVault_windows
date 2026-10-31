#include <gtest/gtest.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include "storage/app_paths.h"

namespace {

using passvault::storage::MoveFileIfMissing;

void WriteFile(const QString& path, const QByteArray& contents) {
    ASSERT_TRUE(QDir().mkpath(QFileInfo(path).dir().absolutePath()));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write(contents), contents.size());
}

QByteArray ReadFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

}  // namespace

TEST(AppPathsTest, MoveFileIfMissingMovesFileAndCreatesParent) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString source = dir.filePath("legacy/google_token.dat");
    const QString target = dir.filePath("current/google_token.dat");
    WriteFile(source, QByteArrayLiteral("token"));

    EXPECT_TRUE(MoveFileIfMissing(source, target));

    EXPECT_FALSE(QFile::exists(source));
    EXPECT_EQ(ReadFile(target), QByteArrayLiteral("token"));
}

TEST(AppPathsTest, MoveFileIfMissingKeepsExistingTargetAndSource) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString source = dir.filePath("legacy/google_token.dat");
    const QString target = dir.filePath("current/google_token.dat");
    WriteFile(source, QByteArrayLiteral("legacy"));
    WriteFile(target, QByteArrayLiteral("current"));

    EXPECT_TRUE(MoveFileIfMissing(source, target));

    EXPECT_TRUE(QFile::exists(source));
    EXPECT_EQ(ReadFile(target), QByteArrayLiteral("current"));
}

TEST(AppPathsTest, MoveFileIfMissingReturnsFalseWhenSourceMissing) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString source = dir.filePath("legacy/google_token.dat");
    const QString target = dir.filePath("current/google_token.dat");

    EXPECT_FALSE(MoveFileIfMissing(source, target));
    EXPECT_FALSE(QFile::exists(target));
}

TEST(AppPathsTest, MoveFileIfMissingReturnsTrueWhenSourceEqualsTarget) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath("google_token.dat");
    WriteFile(path, QByteArrayLiteral("token"));

    EXPECT_TRUE(MoveFileIfMissing(path, path));
    EXPECT_EQ(ReadFile(path), QByteArrayLiteral("token"));
}
