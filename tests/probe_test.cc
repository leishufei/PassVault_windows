#include <gtest/gtest.h>

#include <QByteArray>
#include <QTemporaryDir>

#include <string>

#include "crypto/crypto_service.h"
#include "crypto/random.h"
#include "crypto/session_key.h"
#include "master_password/master_password_manager.h"
#include "master_password/master_password_store.h"
#include "model/password_entry.h"
#include "storage/database.h"
#include "storage/password_dao.h"
#include "storage/schema.h"
#include "storage/category_dao.h"
#include "master_password/password_change_coordinator.h"

namespace {

using passvault::crypto::CryptoService;

class ProbeHangTest : public ::testing::Test {
 protected:
    void SetUp() override {
        store_ = std::make_unique<passvault::master_password::MasterPasswordStore>(
            dir_.filePath("master.dat"));
        db_ = passvault::storage::Database::OpenInMemory();
        passvault::storage::EnsureCurrentSchema(*db_);
        dao_ = std::make_unique<passvault::storage::PasswordDao>(*db_);
        manager_ =
            std::make_unique<passvault::master_password::MasterPasswordManager>(
                *store_);
        categories_ = std::make_unique<passvault::storage::CategoryDao>(*db_);
        coordinator_ = std::make_unique<passvault::master_password::PasswordChangeCoordinator>(
            *store_, *manager_, *dao_, *categories_, []() { return true; });
        ASSERT_TRUE(coordinator_->SaveBinding(passvault::master_password::CloudBinding::kLocal));
    }

    std::int64_t InsertEncrypted(const passvault::crypto::SessionKey& key,
                                 const QString& plain, const QString& uuid) {
        const QByteArray pt = plain.toUtf8();
        const auto iv = passvault::crypto::Random::Bytes(CryptoService::kIvSize);
        const QByteArray ct = CryptoService::EncryptGcm(
            key.data(), key.size(), iv.data(), iv.size(),
            reinterpret_cast<const std::uint8_t*>(pt.constData()),
            static_cast<std::size_t>(pt.size()));
        passvault::model::PasswordEntry entry;
        entry.uuid = uuid;
        entry.title = QStringLiteral("t");
        entry.encrypted_password = ct;
        entry.password_iv = QByteArray(
            reinterpret_cast<const char*>(iv.data()),
            static_cast<int>(iv.size()));
        return dao_->Insert(entry).value_or(-1);
    }

    std::unique_ptr<passvault::storage::CategoryDao> categories_;
    std::unique_ptr<passvault::master_password::PasswordChangeCoordinator> coordinator_;
    QTemporaryDir dir_;
    std::unique_ptr<passvault::master_password::MasterPasswordStore> store_;
    std::unique_ptr<passvault::storage::Database> db_;
    std::unique_ptr<passvault::storage::PasswordDao> dao_;
    std::unique_ptr<passvault::master_password::MasterPasswordManager> manager_;
};

TEST_F(ProbeHangTest, First) {
    const auto initial = manager_->SetInitial(std::string(12, 'i'));
    ASSERT_TRUE(initial.has_value());
    InsertEncrypted(initial->session_key, QStringLiteral("secret-a"),
                    QStringLiteral("uuid-a"));
    const auto b = InsertEncrypted(initial->session_key,
                                   QStringLiteral("secret-b"),
                                   QStringLiteral("uuid-b"));
    dao_->LogicalDelete(b, 1);
    passvault::model::PasswordEntry empty;
    empty.uuid = QStringLiteral("uuid-e");
    empty.title = QStringLiteral("empty");
    dao_->Insert(empty);
    const auto r = coordinator_->Change(QString(12, QLatin1Char('i')),
                                          QString(12, QLatin1Char('r')),
                                          QString(12, QLatin1Char('r')), nullptr);
    EXPECT_EQ(r.outcome, passvault::master_password::PasswordChangeCoordinator::Outcome::kUnchanged);
}

TEST_F(ProbeHangTest, Second) {
    const auto initial = manager_->SetInitial(std::string(12, 'i'));
    ASSERT_TRUE(initial.has_value());
    const auto a = InsertEncrypted(initial->session_key,
                                   QStringLiteral("secret-a"),
                                   QStringLiteral("uuid-a"));
    const auto fb = passvault::crypto::Random::Bytes(32);
    const auto foreign = passvault::crypto::SessionKey::FromSecureBytes(
        passvault::crypto::SecureBytes(fb.data(), fb.size()));
    ASSERT_TRUE(foreign.has_value());
    InsertEncrypted(*foreign, QStringLiteral("secret-f"),
                    QStringLiteral("uuid-f"));
    const auto rb = store_->Load();
    ASSERT_TRUE(rb.has_value());
    const auto r = coordinator_->Change(QString(12, QLatin1Char('i')),
                                           QString(12, QLatin1Char('r')),
                                          QString(12, QLatin1Char('r')), nullptr);
    EXPECT_EQ(r.outcome, passvault::master_password::PasswordChangeCoordinator::Outcome::kUnchanged);
    const auto ra = store_->Load();
    ASSERT_TRUE(ra.has_value());
    const auto e = dao_->FindById(a);
    ASSERT_TRUE(e.has_value());
    auto plain = CryptoService::DecryptGcm(
        initial->session_key.data(), initial->session_key.size(),
        reinterpret_cast<const std::uint8_t*>(e->password_iv.constData()),
        static_cast<std::size_t>(e->password_iv.size()),
        reinterpret_cast<const std::uint8_t*>(
            e->encrypted_password.constData()),
        static_cast<std::size_t>(e->encrypted_password.size()));
    EXPECT_TRUE(plain.has_value());
    EXPECT_TRUE(manager_->VerifyLocal(std::string(12, 'i')).has_value());
}

}  // namespace
