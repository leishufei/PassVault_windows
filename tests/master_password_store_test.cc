#include <gtest/gtest.h>

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QTemporaryDir>

#include "master_password/master_password_store.h"

namespace {

using passvault::master_password::MasterPasswordStore;
using passvault::master_password::MasterRecord;

MasterRecord MakeRecord() {
    MasterRecord r;
    r.password_hash = QByteArray(MasterPasswordStore::kPasswordHashHexLen, 'a');
    r.kdf_salt = QByteArray(MasterPasswordStore::kKdfSaltSize, '\x11');
    return r;
}

}  // namespace

TEST(MasterPasswordStore, RoundTripSaveAndLoad) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    MasterPasswordStore store(dir.filePath("master.dat"));

    EXPECT_FALSE(store.Exists());
    const auto record = MakeRecord();
    ASSERT_TRUE(store.Save(record));
    EXPECT_TRUE(store.Exists());

    const auto loaded = store.Load();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->password_hash, record.password_hash);
    EXPECT_EQ(loaded->kdf_salt, record.kdf_salt);
}

TEST(MasterPasswordStore, LoadMissingReturnsNullopt) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    MasterPasswordStore store(dir.filePath("master.dat"));
    EXPECT_FALSE(store.Load().has_value());
}

TEST(MasterPasswordStore, SaveRejectsBadSizes) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    MasterPasswordStore store(dir.filePath("master.dat"));
    MasterRecord r;
    r.password_hash = QByteArrayLiteral("short");
    r.kdf_salt = QByteArray(MasterPasswordStore::kKdfSaltSize, 'x');
    EXPECT_FALSE(store.Save(r));

    r.password_hash = QByteArray(MasterPasswordStore::kPasswordHashHexLen, 'a');
    r.kdf_salt = QByteArrayLiteral("short");
    EXPECT_FALSE(store.Save(r));
}

TEST(MasterPasswordStore, ClearRemovesFile) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    MasterPasswordStore store(dir.filePath("master.dat"));
    ASSERT_TRUE(store.Save(MakeRecord()));
    ASSERT_TRUE(store.Exists());
    EXPECT_TRUE(store.Clear());
    EXPECT_FALSE(store.Exists());
    EXPECT_TRUE(store.Clear());
}

TEST(MasterPasswordStore, TamperedCiphertextFailsToLoad) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath("master.dat");
    MasterPasswordStore store(path);
    ASSERT_TRUE(store.Save(MakeRecord()));

    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadWrite));
    QByteArray blob = file.readAll();
    ASSERT_GT(blob.size(), 32);
    blob[blob.size() - 1] = blob[blob.size() - 1] ^ 0xff;
    file.seek(0);
    ASSERT_EQ(file.write(blob), blob.size());
    file.close();

    EXPECT_FALSE(store.Load().has_value());
}

TEST(MasterPasswordStore, CreatesParentDir) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString nested = dir.filePath("a/b/c/master.dat");
    MasterPasswordStore store(nested);
    EXPECT_TRUE(store.Save(MakeRecord()));
    EXPECT_TRUE(store.Exists());
}
