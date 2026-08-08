#include <gtest/gtest.h>

#include <QByteArray>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <memory>

#include "model/password_entry.h"
#include "storage/database.h"
#include "storage/password_dao.h"
#include "storage/schema.h"

namespace {

using passvault::model::PasswordEntry;
using passvault::storage::Database;
using passvault::storage::EnsureCurrentSchema;
using passvault::storage::PasswordDao;

PasswordEntry MakeEntry(int seed) {
    PasswordEntry e;
    e.uuid = QStringLiteral("uuid-%1").arg(seed);
    e.title = QStringLiteral("title-%1").arg(seed);
    e.username = QStringLiteral("user-%1").arg(seed);
    e.encrypted_password = QByteArrayLiteral("enc-\x01\x02");
    e.password_iv = QByteArrayLiteral("iv-\x03\x04");
    e.website = QStringLiteral("https://ex-%1.com").arg(seed);
    e.app_package_name = QStringLiteral("com.app.%1").arg(seed);
    e.notes = QStringLiteral("notes-%1").arg(seed);
    e.is_favorite = (seed % 2 == 0);
    e.icon_color = 0x100000 + seed;
    e.strength = seed % 4;
    e.category_id = 0;
    e.created_at = 1700000000000LL + seed * 1000;
    e.updated_at = 1710000000000LL + seed * 1000;
    e.is_deleted = false;
    return e;
}

class PasswordDaoTest : public ::testing::Test {
 protected:
    void SetUp() override {
        db_ = Database::OpenInMemory();
        EnsureCurrentSchema(*db_);
        dao_ = std::make_unique<PasswordDao>(*db_);
    }

    std::unique_ptr<Database> db_;
    std::unique_ptr<PasswordDao> dao_;
};

}  // namespace

TEST_F(PasswordDaoTest, InsertAssignsRowidWhenIdZero) {
    const auto id1 = dao_->Insert(MakeEntry(1));
    const auto id2 = dao_->Insert(MakeEntry(2));
    ASSERT_TRUE(id1.has_value());
    ASSERT_TRUE(id2.has_value());
    EXPECT_GT(*id1, 0);
    EXPECT_NE(*id1, *id2);
}

TEST_F(PasswordDaoTest, FindByIdAndUuidRoundTrip) {
    auto entry = MakeEntry(1);
    const auto id = dao_->Insert(entry);
    ASSERT_TRUE(id.has_value());

    const auto by_id = dao_->FindById(*id);
    ASSERT_TRUE(by_id.has_value());
    EXPECT_EQ(by_id->uuid, entry.uuid);
    EXPECT_EQ(by_id->title, entry.title);
    EXPECT_EQ(by_id->username, entry.username);
    EXPECT_EQ(by_id->encrypted_password, entry.encrypted_password);
    EXPECT_EQ(by_id->password_iv, entry.password_iv);
    EXPECT_EQ(by_id->website, entry.website);
    EXPECT_EQ(by_id->app_package_name, entry.app_package_name);
    EXPECT_EQ(by_id->notes, entry.notes);
    EXPECT_EQ(by_id->is_favorite, entry.is_favorite);
    EXPECT_EQ(by_id->icon_color, entry.icon_color);
    EXPECT_EQ(by_id->strength, entry.strength);
    EXPECT_EQ(by_id->created_at, entry.created_at);
    EXPECT_EQ(by_id->updated_at, entry.updated_at);
    EXPECT_FALSE(by_id->is_deleted);

    const auto by_uuid = dao_->FindByUuid(entry.uuid);
    ASSERT_TRUE(by_uuid.has_value());
    EXPECT_EQ(by_uuid->id, *id);
}

TEST_F(PasswordDaoTest, FindMissingReturnsNullopt) {
    EXPECT_FALSE(dao_->FindById(999).has_value());
    EXPECT_FALSE(dao_->FindByUuid(QStringLiteral("nope")).has_value());
}

TEST_F(PasswordDaoTest, ListActiveExcludesTombstonesAndOrdersByCreatedDesc) {
    auto e1 = MakeEntry(1);
    e1.created_at = 1000;
    auto e2 = MakeEntry(2);
    e2.created_at = 3000;
    auto e3 = MakeEntry(3);
    e3.created_at = 2000;
    e3.is_deleted = true;
    ASSERT_TRUE(dao_->Insert(e1).has_value());
    ASSERT_TRUE(dao_->Insert(e2).has_value());
    ASSERT_TRUE(dao_->Insert(e3).has_value());

    const auto list = dao_->ListActive();
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].uuid, e2.uuid);
    EXPECT_EQ(list[1].uuid, e1.uuid);
}

TEST_F(PasswordDaoTest, ListIncludingDeletedReturnsAll) {
    auto e1 = MakeEntry(1);
    auto e2 = MakeEntry(2);
    e2.is_deleted = true;
    ASSERT_TRUE(dao_->Insert(e1).has_value());
    ASSERT_TRUE(dao_->Insert(e2).has_value());
    EXPECT_EQ(dao_->ListIncludingDeleted().size(), 2u);
}

TEST_F(PasswordDaoTest, InsertManyIsAtomic) {
    std::vector<PasswordEntry> entries = {MakeEntry(1), MakeEntry(2),
                                          MakeEntry(3)};
    ASSERT_TRUE(dao_->InsertMany(entries));
    EXPECT_EQ(dao_->ListIncludingDeleted().size(), 3u);
}

TEST_F(PasswordDaoTest, UpdateModifiesFields) {
    auto entry = MakeEntry(1);
    const auto id = dao_->Insert(entry);
    ASSERT_TRUE(id.has_value());
    entry.id = *id;
    entry.title = QStringLiteral("changed");
    entry.notes = QStringLiteral("new notes");
    entry.is_favorite = !entry.is_favorite;
    entry.updated_at += 1;
    EXPECT_TRUE(dao_->Update(entry));

    const auto reread = dao_->FindById(*id);
    ASSERT_TRUE(reread.has_value());
    EXPECT_EQ(reread->title, QStringLiteral("changed"));
    EXPECT_EQ(reread->notes, QStringLiteral("new notes"));
    EXPECT_EQ(reread->is_favorite, entry.is_favorite);
    EXPECT_EQ(reread->updated_at, entry.updated_at);
}

TEST_F(PasswordDaoTest, SetFavoriteToggles) {
    auto entry = MakeEntry(1);
    entry.is_favorite = false;
    const auto id = dao_->Insert(entry);
    ASSERT_TRUE(id.has_value());
    EXPECT_TRUE(dao_->SetFavorite(*id, true));
    EXPECT_TRUE(dao_->FindById(*id)->is_favorite);
    EXPECT_TRUE(dao_->SetFavorite(*id, false));
    EXPECT_FALSE(dao_->FindById(*id)->is_favorite);
}

TEST_F(PasswordDaoTest, HardDeleteRemovesRow) {
    const auto id = dao_->Insert(MakeEntry(1));
    ASSERT_TRUE(id.has_value());
    EXPECT_TRUE(dao_->HardDelete(*id));
    EXPECT_FALSE(dao_->FindById(*id).has_value());
    EXPECT_FALSE(dao_->HardDelete(*id));
}

TEST_F(PasswordDaoTest, LogicalDeleteMarksAndUpdatesTimestamp) {
    const auto id = dao_->Insert(MakeEntry(1));
    ASSERT_TRUE(id.has_value());
    EXPECT_TRUE(dao_->LogicalDelete(*id, 9999));
    const auto e = dao_->FindById(*id);
    ASSERT_TRUE(e.has_value());
    EXPECT_TRUE(e->is_deleted);
    EXPECT_EQ(e->updated_at, 9999);
}

TEST_F(PasswordDaoTest, DeleteAllClearsRows) {
    ASSERT_TRUE(dao_->Insert(MakeEntry(1)).has_value());
    ASSERT_TRUE(dao_->Insert(MakeEntry(2)).has_value());
    EXPECT_TRUE(dao_->DeleteAll());
    EXPECT_EQ(dao_->ListIncludingDeleted().size(), 0u);
}

TEST_F(PasswordDaoTest, SearchMatchesTitleUsernameWebsiteOrNotes) {
    auto e1 = MakeEntry(1);
    e1.title = QStringLiteral("GitHub");
    e1.username = QStringLiteral("octocat");
    auto e2 = MakeEntry(2);
    e2.title = QStringLiteral("Gitlab");
    e2.username = QStringLiteral("nobody");
    auto e3 = MakeEntry(3);
    e3.title = QStringLiteral("Foo");
    e3.username = QStringLiteral("bar");
    e3.website = QStringLiteral("https://octopus.dev");
    auto e4 = MakeEntry(4);
    e4.notes = QStringLiteral("private-octagon-note");
    e4.updated_at = e3.updated_at + 10000;
    ASSERT_TRUE(dao_->Insert(e1).has_value());
    ASSERT_TRUE(dao_->Insert(e2).has_value());
    ASSERT_TRUE(dao_->Insert(e3).has_value());
    ASSERT_TRUE(dao_->Insert(e4).has_value());

    EXPECT_EQ(dao_->Search(QStringLiteral("git")).size(), 2u);
    EXPECT_EQ(dao_->Search(QStringLiteral("octo")).size(), 2u);
    const auto notes_match =
        dao_->Search(QStringLiteral("private-octagon-note"));
    ASSERT_EQ(notes_match.size(), 1u);
    EXPECT_EQ(notes_match.front().uuid, e4.uuid);
    EXPECT_EQ(dao_->Search(QStringLiteral("missing")).size(), 0u);
}

TEST_F(PasswordDaoTest, SearchExcludesTombstones) {
    auto e1 = MakeEntry(1);
    e1.title = QStringLiteral("keyword");
    auto e2 = MakeEntry(2);
    e2.title = QStringLiteral("keyword again");
    e2.is_deleted = true;
    ASSERT_TRUE(dao_->Insert(e1).has_value());
    ASSERT_TRUE(dao_->Insert(e2).has_value());
    EXPECT_EQ(dao_->Search(QStringLiteral("keyword")).size(), 1u);
}

TEST_F(PasswordDaoTest, FindByWebsiteRequiresExactMatch) {
    auto e1 = MakeEntry(1);
    e1.website = QStringLiteral("https://a.com");
    auto e2 = MakeEntry(2);
    e2.website = QStringLiteral("https://a.com/path");
    ASSERT_TRUE(dao_->Insert(e1).has_value());
    ASSERT_TRUE(dao_->Insert(e2).has_value());
    const auto out = dao_->FindByWebsite(QStringLiteral("https://a.com"));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].website, QStringLiteral("https://a.com"));
}

TEST_F(PasswordDaoTest, FindByPackageNameExactMatch) {
    auto e1 = MakeEntry(1);
    e1.app_package_name = QStringLiteral("com.foo");
    auto e2 = MakeEntry(2);
    e2.app_package_name = QStringLiteral("com.bar");
    ASSERT_TRUE(dao_->Insert(e1).has_value());
    ASSERT_TRUE(dao_->Insert(e2).has_value());
    const auto out = dao_->FindByPackageName(QStringLiteral("com.foo"));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].app_package_name, QStringLiteral("com.foo"));
}

TEST_F(PasswordDaoTest, CountDuplicateIgnoresSelfAndTombstonesAndEmpty) {
    auto e1 = MakeEntry(1);
    e1.title = QStringLiteral("dup");
    e1.username = QStringLiteral("user");
    auto e2 = MakeEntry(2);
    e2.title = QStringLiteral("dup");
    e2.username = QStringLiteral("user");
    auto e3 = MakeEntry(3);
    e3.title = QStringLiteral("dup");
    e3.username = QStringLiteral("user");
    e3.is_deleted = true;
    auto e_empty = MakeEntry(4);
    e_empty.title = QStringLiteral("");
    e_empty.username = QStringLiteral("");
    const auto id1 = dao_->Insert(e1);
    const auto id2 = dao_->Insert(e2);
    ASSERT_TRUE(dao_->Insert(e3).has_value());
    ASSERT_TRUE(dao_->Insert(e_empty).has_value());
    ASSERT_TRUE(id1.has_value());
    ASSERT_TRUE(id2.has_value());

    EXPECT_EQ(dao_->CountDuplicate(QStringLiteral("dup"),
                                   QStringLiteral("user"), *id1),
              1);
    EXPECT_EQ(dao_->CountDuplicate(QStringLiteral(""), QStringLiteral(""), 0),
              0);
}

TEST_F(PasswordDaoTest, ListByCategoryAndListIdsByCategory) {
    auto e1 = MakeEntry(1);
    e1.category_id = 10;
    auto e2 = MakeEntry(2);
    e2.category_id = 10;
    auto e3 = MakeEntry(3);
    e3.category_id = 20;
    auto e4 = MakeEntry(4);
    e4.category_id = 10;
    e4.is_deleted = true;
    ASSERT_TRUE(dao_->Insert(e1).has_value());
    ASSERT_TRUE(dao_->Insert(e2).has_value());
    ASSERT_TRUE(dao_->Insert(e3).has_value());
    ASSERT_TRUE(dao_->Insert(e4).has_value());

    EXPECT_EQ(dao_->ListByCategory(10).size(), 2u);
    EXPECT_EQ(dao_->ListByCategory(20).size(), 1u);
    EXPECT_EQ(dao_->ListByCategory(99).size(), 0u);

    const auto ids = dao_->ListIdsByCategory(10);
    EXPECT_EQ(ids.size(), 2u);
}

TEST_F(PasswordDaoTest, MigrateCategoryRewriteEvenTombstones) {
    auto e1 = MakeEntry(1);
    e1.category_id = 5;
    auto e2 = MakeEntry(2);
    e2.category_id = 5;
    e2.is_deleted = true;
    ASSERT_TRUE(dao_->Insert(e1).has_value());
    ASSERT_TRUE(dao_->Insert(e2).has_value());
    EXPECT_TRUE(dao_->MigrateCategory(5, 6));
    for (const auto& e : dao_->ListIncludingDeleted()) {
        EXPECT_EQ(e.category_id, 6);
    }
}

TEST_F(PasswordDaoTest, BulkMigrateCategorySkipsTombstonesAndUpdatesTimestamp) {
    auto e1 = MakeEntry(1);
    e1.category_id = 5;
    e1.updated_at = 100;
    auto e2 = MakeEntry(2);
    e2.category_id = 5;
    e2.is_deleted = true;
    e2.updated_at = 100;
    ASSERT_TRUE(dao_->Insert(e1).has_value());
    ASSERT_TRUE(dao_->Insert(e2).has_value());

    EXPECT_TRUE(dao_->BulkMigrateCategory(5, 6, 999));

    const auto reread_e1 = dao_->FindByUuid(e1.uuid);
    ASSERT_TRUE(reread_e1.has_value());
    EXPECT_EQ(reread_e1->category_id, 6);
    EXPECT_EQ(reread_e1->updated_at, 999);

    const auto reread_e2 = dao_->FindByUuid(e2.uuid);
    ASSERT_TRUE(reread_e2.has_value());
    EXPECT_EQ(reread_e2->category_id, 5);
    EXPECT_EQ(reread_e2->updated_at, 100);
}

TEST_F(PasswordDaoTest, BulkLogicalDeleteMarksListedIds) {
    const auto id1 = dao_->Insert(MakeEntry(1));
    const auto id2 = dao_->Insert(MakeEntry(2));
    const auto id3 = dao_->Insert(MakeEntry(3));
    ASSERT_TRUE(id1.has_value());
    ASSERT_TRUE(id2.has_value());
    ASSERT_TRUE(id3.has_value());
    EXPECT_TRUE(dao_->BulkLogicalDelete({*id1, *id3}, 555));
    EXPECT_TRUE(dao_->FindById(*id1)->is_deleted);
    EXPECT_FALSE(dao_->FindById(*id2)->is_deleted);
    EXPECT_TRUE(dao_->FindById(*id3)->is_deleted);
    EXPECT_EQ(dao_->FindById(*id1)->updated_at, 555);
}

TEST_F(PasswordDaoTest, BulkLogicalDeleteEmptyIsNoop) {
    ASSERT_TRUE(dao_->Insert(MakeEntry(1)).has_value());
    EXPECT_TRUE(dao_->BulkLogicalDelete({}, 100));
    EXPECT_FALSE(dao_->ListActive().empty());
}

TEST_F(PasswordDaoTest, BulkUpdateCategoryByIds) {
    const auto id1 = dao_->Insert(MakeEntry(1));
    const auto id2 = dao_->Insert(MakeEntry(2));
    ASSERT_TRUE(id1.has_value());
    ASSERT_TRUE(id2.has_value());
    EXPECT_TRUE(dao_->BulkUpdateCategoryByIds({*id1, *id2}, 77, 1234));
    EXPECT_EQ(dao_->FindById(*id1)->category_id, 77);
    EXPECT_EQ(dao_->FindById(*id2)->category_id, 77);
    EXPECT_EQ(dao_->FindById(*id1)->updated_at, 1234);
}

TEST_F(PasswordDaoTest, UpdateCategoryOfSingle) {
    const auto id = dao_->Insert(MakeEntry(1));
    ASSERT_TRUE(id.has_value());
    EXPECT_TRUE(dao_->UpdateCategoryOf(*id, 42, 7777));
    const auto e = dao_->FindById(*id);
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->category_id, 42);
    EXPECT_EQ(e->updated_at, 7777);
}

TEST_F(PasswordDaoTest, DeleteOldTombstonesRemovesExpiredOnly) {
    auto e_live = MakeEntry(1);
    auto e_recent = MakeEntry(2);
    e_recent.is_deleted = true;
    e_recent.updated_at = 200;
    auto e_old = MakeEntry(3);
    e_old.is_deleted = true;
    e_old.updated_at = 50;
    ASSERT_TRUE(dao_->Insert(e_live).has_value());
    ASSERT_TRUE(dao_->Insert(e_recent).has_value());
    ASSERT_TRUE(dao_->Insert(e_old).has_value());

    EXPECT_TRUE(dao_->DeleteOldTombstones(100));
    const auto all = dao_->ListIncludingDeleted();
    EXPECT_EQ(all.size(), 2u);
    EXPECT_TRUE(std::none_of(all.begin(), all.end(),
                             [](const PasswordEntry& e) {
                                 return e.uuid ==
                                        QStringLiteral("uuid-3");
                             }));
}
