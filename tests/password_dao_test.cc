#include <gtest/gtest.h>

#include <sqlite3.h>

#include <QByteArray>
#include <QString>
#include <QDir>
#include <QUuid>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string_view>

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

TEST_F(PasswordDaoTest, FavoriteChangesSyncVersionButPreservesContentTimestamp) {
    const auto initial = MakeEntry(1);
    const auto id = dao_->Insert(initial);
    ASSERT_TRUE(id);
    for (const bool favorite : {true, false}) {
        const auto before = dao_->FindById(*id);
        ASSERT_TRUE(before);
        ASSERT_TRUE(dao_->SetFavorite(*id, favorite));
        const auto after = dao_->FindById(*id);
        ASSERT_TRUE(after);
        EXPECT_EQ(after->is_favorite, favorite);
        EXPECT_GT(after->updated_at, before->updated_at);
        EXPECT_EQ(after->ContentUpdatedAt(), initial.updated_at);
        EXPECT_EQ(after->encrypted_password, initial.encrypted_password);
        EXPECT_EQ(after->password_iv, initial.password_iv);
    }
    auto current = dao_->FindById(*id);
    ASSERT_TRUE(current);
    const auto version = current->updated_at;
    ASSERT_TRUE(dao_->SetFavorite(*id, false));
    EXPECT_EQ(dao_->FindById(*id)->updated_at, version);
    current->notes = QStringLiteral("changed content");
    current->updated_at = version + 1;
    ASSERT_TRUE(dao_->Update(*current));
    EXPECT_EQ(dao_->FindById(*id)->ContentUpdatedAt(), version + 1);
}

TEST_F(PasswordDaoTest, FailedFavoriteTimestampWriteRollsBackFavoriteAndVersion) {
    const auto initial = MakeEntry(1);
    const auto id = dao_->Insert(initial);
    ASSERT_TRUE(id);
    ASSERT_TRUE(db_->Execute(
        "CREATE TRIGGER reject_content_time BEFORE INSERT ON "
        "password_content_updates BEGIN SELECT RAISE(ABORT, 'blocked'); END"));
    EXPECT_FALSE(dao_->SetFavorite(*id, true));
    const auto after = dao_->FindById(*id);
    ASSERT_TRUE(after);
    EXPECT_FALSE(after->is_favorite);
    EXPECT_EQ(after->updated_at, initial.updated_at);
    EXPECT_EQ(after->ContentUpdatedAt(), initial.updated_at);
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

TEST_F(PasswordDaoTest, CategoryDuplicateIgnoresWebsiteSelfAndTombstones) {
    auto entry = MakeEntry(1);
    const auto id = dao_->Insert(entry);
    ASSERT_TRUE(id);
    EXPECT_EQ(dao_->HasDuplicateInCategory(0, entry.title, entry.username, -1), true);
    EXPECT_EQ(dao_->HasDuplicateInCategory(0, entry.title, entry.username, *id), false);
    EXPECT_EQ(dao_->HasDuplicateInCategory(1, entry.title, entry.username, -1), false);
    EXPECT_EQ(dao_->HasDuplicateInCategory(
                  0, QStringLiteral("other"), entry.username, -1), false);
    EXPECT_EQ(dao_->HasDuplicateInCategory(
                  0, entry.title, QStringLiteral("other"), -1), false);

    auto duplicate = entry;
    duplicate.uuid = QStringLiteral("duplicate");
    duplicate.website = QStringLiteral("https://different.example");
    const auto duplicate_id = dao_->Insert(duplicate);
    ASSERT_TRUE(duplicate_id);
    EXPECT_EQ(dao_->HasDuplicateInCategory(0, entry.title, entry.username, *id), true);
    ASSERT_TRUE(dao_->LogicalDelete(*duplicate_id, 1800000000000LL));
    EXPECT_EQ(dao_->HasDuplicateInCategory(0, entry.title, entry.username, *id), false);

    entry.id = *id;
    entry.username.clear();
    ASSERT_TRUE(dao_->Update(entry));
    EXPECT_EQ(dao_->HasDuplicateInCategory(0, entry.title, QString(), -1), true);
}

TEST_F(PasswordDaoTest, CategoryDuplicateReportsReadFailure) {
    ASSERT_TRUE(db_->Execute("DROP TABLE password_entries"));
    EXPECT_FALSE(dao_->HasDuplicateInCategory(
        0, QStringLiteral("title"), QStringLiteral("user"), -1).has_value());
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

TEST_F(PasswordDaoTest, DeleteByCategoryPreservesCiphertextAndOldTombstones) {
    for (int i = 1; i <= 4; ++i) {
        auto entry = MakeEntry(i);
        entry.category_id = i == 4 ? 6 : 5;
        entry.is_deleted = i == 3;
        entry.updated_at = 100;
        ASSERT_TRUE(dao_->Insert(entry).has_value());
    }
    const auto before = dao_->ListIncludingDeleted();
    ASSERT_TRUE(dao_->LogicalDeleteByCategory(5, 999));
    for (const auto& entry : before) {
        const auto after = dao_->FindById(entry.id);
        ASSERT_TRUE(after.has_value());
        EXPECT_EQ(after->category_id, entry.category_id);
        EXPECT_EQ(after->encrypted_password, entry.encrypted_password);
        EXPECT_EQ(after->password_iv, entry.password_iv);
        const bool newly_deleted = entry.category_id == 5 && !entry.is_deleted;
        EXPECT_EQ(after->is_deleted, entry.is_deleted || newly_deleted);
        EXPECT_EQ(after->updated_at, newly_deleted ? 999 : 100);
    }
    EXPECT_TRUE(dao_->LogicalDeleteByCategory(99, 1000));
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

TEST_F(PasswordDaoTest, RestorePreservesContentAndUsesOnlyActiveCategories) {
    ASSERT_TRUE(db_->Execute("INSERT INTO categories(id,uuid,name,color,createdAt,updatedAt,isDeleted) "
                            "VALUES(1,'active','active',0,0,0,0),(2,'deleted','deleted',0,0,0,1)"));
    std::vector<std::int64_t> ids;
    for (int i = 1; i <= 3; ++i) {
        auto entry = MakeEntry(i);
        entry.category_id = i;
        entry.is_deleted = true;
        ids.push_back(*dao_->Insert(entry));
    }
    auto targets = ids;
    targets.push_back(ids.front());
    targets.push_back(9999);
    EXPECT_EQ(dao_->BulkRestore(targets, 1800000000000LL), 3);
    for (int i = 1; i <= 3; ++i) {
        const auto restored = dao_->FindById(ids[i - 1]);
        ASSERT_TRUE(restored);
        const auto original = MakeEntry(i);
        EXPECT_EQ(restored->uuid, original.uuid);
        EXPECT_EQ(restored->encrypted_password, original.encrypted_password);
        EXPECT_EQ(restored->password_iv, original.password_iv);
        EXPECT_EQ(restored->created_at, original.created_at);
        EXPECT_EQ(restored->title, original.title);
        EXPECT_EQ(restored->username, original.username);
        EXPECT_EQ(restored->notes, original.notes);
        EXPECT_EQ(restored->website, original.website);
        EXPECT_EQ(restored->app_package_name, original.app_package_name);
        EXPECT_EQ(restored->is_favorite, original.is_favorite);
        EXPECT_EQ(restored->icon_color, original.icon_color);
        EXPECT_EQ(restored->strength, original.strength);
        EXPECT_EQ(restored->category_id, i == 1 ? 1 : 0);
        EXPECT_EQ(restored->updated_at, 1800000000000LL);
        EXPECT_FALSE(restored->is_deleted);
    }
    EXPECT_EQ(dao_->BulkRestore(ids, 1800000000001LL), 0);
}

TEST_F(PasswordDaoTest, TrashMutationsRollBackAfterSecondRowFailure) {
    auto first = MakeEntry(1);
    auto second = MakeEntry(2);
    first.is_deleted = second.is_deleted = true;
    const auto a = *dao_->Insert(first);
    const auto b = *dao_->Insert(second);
    ASSERT_TRUE(db_->Execute("CREATE TRIGGER fail_restore BEFORE UPDATE ON password_entries "
                            "WHEN OLD.uuid = 'uuid-2' BEGIN SELECT RAISE(ABORT,'injected'); END"));
    EXPECT_FALSE(dao_->BulkRestore({a, b}, 1800000000000LL));
    EXPECT_TRUE(dao_->FindById(a)->is_deleted);
    EXPECT_EQ(dao_->FindById(a)->updated_at, first.updated_at);
    ASSERT_TRUE(db_->Execute("CREATE TRIGGER fail_delete BEFORE DELETE ON password_entries "
                            "WHEN OLD.uuid = 'uuid-2' BEGIN SELECT RAISE(ABORT,'injected'); END"));
    EXPECT_FALSE(dao_->BulkPermanentlyDelete({a, b}, 1800000000000LL));
    EXPECT_FALSE(dao_->EmptyTrash(1800000000000LL));
    EXPECT_TRUE(dao_->FindById(a));
    EXPECT_TRUE(dao_->FindById(b));
    ASSERT_TRUE(dao_->ListDeletionMarkers());
    EXPECT_TRUE(dao_->ListDeletionMarkers()->empty());
}

TEST_F(PasswordDaoTest, PermanentDeleteAndEmptyTrashRetainOnlyInvisibleMarkers) {
    const auto active = *dao_->Insert(MakeEntry(1));
    auto deleted = MakeEntry(2);
    deleted.is_deleted = true;
    const auto a = *dao_->Insert(deleted);
    deleted.uuid = QStringLiteral("other");
    const auto b = *dao_->Insert(deleted);
    EXPECT_EQ(dao_->BulkPermanentlyDelete({a, a, active, 9999}, 1800000000000LL), 1);
    EXPECT_FALSE(dao_->FindById(a));
    EXPECT_FALSE(dao_->FindByUuid(QStringLiteral("uuid-2")));
    EXPECT_TRUE(dao_->Search(QStringLiteral("title-2")).empty());
    EXPECT_EQ(dao_->ListIncludingDeleted().size(), 2u);
    EXPECT_EQ(dao_->EmptyTrash(1800000000001LL), 1);
    EXPECT_FALSE(dao_->FindById(b));
    EXPECT_TRUE(dao_->FindById(active));
    EXPECT_EQ(dao_->ListDeletionMarkers()->size(), 2u);
    EXPECT_TRUE(dao_->DeleteOldTombstones(1800000000000LL));
    EXPECT_EQ(dao_->ListDeletionMarkers()->size(), 2u);
    EXPECT_TRUE(dao_->DeleteOldTombstones(1800000000001LL));
    ASSERT_EQ(dao_->ListDeletionMarkers()->size(), 1u);
    EXPECT_EQ(dao_->ListDeletionMarkers()->front().uuid, QStringLiteral("other"));
}

TEST(PasswordDeletionPersistence, MarkerSurvivesReopenAndSchemaRemainsV8) {
    const QString root = qEnvironmentVariable("PASSVAULT_TEST_ARTIFACT_DIR",
        QDir::currentPath() + QStringLiteral("/build/diagnostics/trash-persistence"));
    ASSERT_TRUE(QDir().mkpath(root));
    const QString path = root + QStringLiteral("/trash-") +
        QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".db");
    {
        auto db = Database::Open(path);
        EnsureCurrentSchema(*db);
        PasswordDao dao(*db);
        auto entry = MakeEntry(1);
        entry.is_deleted = true;
        const auto id = dao.Insert(entry);
        ASSERT_TRUE(id);
        ASSERT_EQ(dao.BulkPermanentlyDelete({*id}, 1800000000000LL), 1);
    }
    {
        auto db = Database::Open(path);
        EnsureCurrentSchema(*db);
        EXPECT_EQ(db->UserVersion(), 8);
        PasswordDao dao(*db);
        EXPECT_TRUE(dao.ListIncludingDeleted().empty());
        ASSERT_TRUE(dao.ListDeletionMarkers());
        ASSERT_EQ(dao.ListDeletionMarkers()->size(), 1u);
        EXPECT_EQ(dao.ListDeletionMarkers()->front().uuid, QStringLiteral("uuid-1"));
        EXPECT_EQ(dao.ListDeletionMarkers()->front().deleted_at, 1800000000000LL);
    }
}

TEST_F(PasswordDaoTest, DeletionMarkerRetentionKeepsExactThirtyDayBoundary) {
    constexpr std::int64_t now = 1800000000000LL;
    constexpr std::int64_t cutoff = now - 30LL * 24 * 60 * 60 * 1000;
    ASSERT_TRUE(dao_->ApplyDeletionMarker(QStringLiteral("expired"), cutoff - 1));
    ASSERT_TRUE(dao_->ApplyDeletionMarker(QStringLiteral("boundary"), cutoff));
    ASSERT_TRUE(dao_->ApplyDeletionMarker(QStringLiteral("recent"), cutoff + 1));
    ASSERT_TRUE(dao_->DeleteOldTombstones(cutoff));
    const auto markers = dao_->ListDeletionMarkers();
    ASSERT_TRUE(markers);
    ASSERT_EQ(markers->size(), 2u);
    for (const auto& marker : *markers) EXPECT_GE(marker.deleted_at, cutoff);
}

TEST_F(PasswordDaoTest, SyncedEntryCannotReplaceOlderOrEqualDeletionMarker) {
    auto entry = MakeEntry(1);
    const auto deleted_at = entry.updated_at;
    ASSERT_TRUE(dao_->ApplyDeletionMarker(entry.uuid, deleted_at));
    for (const auto offset : {-1, 0}) {
        entry.updated_at = deleted_at + offset;
        ASSERT_TRUE(dao_->ApplySyncedEntry(entry));
        EXPECT_FALSE(dao_->FindByUuid(entry.uuid));
        const auto markers = dao_->ListDeletionMarkers();
        ASSERT_TRUE(markers);
        ASSERT_EQ(markers->size(), 1u);
        EXPECT_EQ(markers->front().deleted_at, deleted_at);
    }
}

TEST_F(PasswordDaoTest, SyncedEntryStrictlyNewerThanMarkerRestoresContent) {
    auto entry = MakeEntry(1);
    ASSERT_TRUE(dao_->ApplyDeletionMarker(entry.uuid, entry.updated_at - 1));
    ASSERT_TRUE(dao_->ApplySyncedEntry(entry));
    const auto restored = dao_->FindByUuid(entry.uuid);
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored->encrypted_password, entry.encrypted_password);
    EXPECT_EQ(restored->password_iv, entry.password_iv);
    EXPECT_EQ(restored->updated_at, entry.updated_at);
    ASSERT_TRUE(dao_->ListDeletionMarkers());
    EXPECT_TRUE(dao_->ListDeletionMarkers()->empty());
}

TEST_F(PasswordDaoTest, SyncedEntryPreservesNewerAndEqualCurrentContent) {
    const auto original = MakeEntry(1);
    ASSERT_TRUE(dao_->Insert(original));
    auto candidate = original;
    candidate.title = QStringLiteral("stale candidate");
    for (const auto offset : {-1, 0}) {
        candidate.updated_at = original.updated_at + offset;
        ASSERT_TRUE(dao_->ApplySyncedEntry(candidate));
        const auto preserved = dao_->FindByUuid(original.uuid);
        ASSERT_TRUE(preserved);
        EXPECT_EQ(preserved->title, original.title);
        EXPECT_EQ(preserved->encrypted_password, original.encrypted_password);
    }
}

TEST_F(PasswordDaoTest, SyncedEntryReadFailureDoesNotTreatMarkerAsMissing) {
    const auto entry = MakeEntry(1);
    ASSERT_TRUE(dao_->ApplyDeletionMarker(entry.uuid, entry.updated_at - 1));
    ASSERT_EQ(sqlite3_set_authorizer(db_->handle(),
        [](void*, int action, const char* table, const char* column, const char*,
           const char*) {
            return action == SQLITE_READ && table && column &&
                    std::string_view(table) == "password_deletion_markers" &&
                    std::string_view(column) == "deletedAt"
                ? SQLITE_DENY : SQLITE_OK;
        }, nullptr), SQLITE_OK);
    const bool applied = dao_->ApplySyncedEntry(entry);
    ASSERT_EQ(sqlite3_set_authorizer(db_->handle(), nullptr, nullptr), SQLITE_OK);
    EXPECT_FALSE(applied);
    EXPECT_FALSE(dao_->FindByUuid(entry.uuid));
    const auto markers = dao_->ListDeletionMarkers();
    ASSERT_TRUE(markers);
    ASSERT_EQ(markers->size(), 1u);
    EXPECT_EQ(markers->front().deleted_at, entry.updated_at - 1);
    EXPECT_EQ(sqlite3_get_autocommit(db_->handle()), 1);
}

TEST_F(PasswordDaoTest, SyncedEntryMarkerRemovalFailureRollsBackInsertedContent) {
    const auto entry = MakeEntry(1);
    ASSERT_TRUE(dao_->ApplyDeletionMarker(entry.uuid, entry.updated_at - 1));
    ASSERT_TRUE(db_->Execute(
        "CREATE TRIGGER fail_marker_remove BEFORE DELETE ON password_deletion_markers "
        "BEGIN SELECT RAISE(ABORT,'injected'); END"));
    EXPECT_FALSE(dao_->ApplySyncedEntry(entry));
    EXPECT_FALSE(dao_->FindByUuid(entry.uuid));
    const auto markers = dao_->ListDeletionMarkers();
    ASSERT_TRUE(markers);
    ASSERT_EQ(markers->size(), 1u);
    EXPECT_EQ(markers->front().deleted_at, entry.updated_at - 1);
    EXPECT_EQ(sqlite3_get_autocommit(db_->handle()), 1);
}
