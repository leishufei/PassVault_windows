#include <gtest/gtest.h>

#include <QString>

#include <memory>

#include "model/category.h"
#include "storage/category_dao.h"
#include "storage/database.h"
#include "storage/schema.h"

namespace {

using passvault::model::Category;
using passvault::storage::CategoryDao;
using passvault::storage::Database;
using passvault::storage::EnsureCurrentSchema;

Category MakeCategory(int seed) {
    Category c;
    c.uuid = QStringLiteral("cat-uuid-%1").arg(seed);
    c.name = QStringLiteral("Category %1").arg(seed);
    c.color = 0x100000 + seed;
    c.is_default = false;
    c.sort_order = seed;
    c.created_at = 1700000000000LL + seed * 1000;
    c.updated_at = 1710000000000LL + seed * 1000;
    c.is_deleted = false;
    return c;
}

class CategoryDaoTest : public ::testing::Test {
 protected:
    void SetUp() override {
        db_ = Database::OpenInMemory();
        EnsureCurrentSchema(*db_);
        dao_ = std::make_unique<CategoryDao>(*db_);
    }

    std::unique_ptr<Database> db_;
    std::unique_ptr<CategoryDao> dao_;
};

}  // namespace

TEST_F(CategoryDaoTest, InsertAssignsRowid) {
    const auto id = dao_->Insert(MakeCategory(1));
    ASSERT_TRUE(id.has_value());
    EXPECT_GT(*id, 0);
}

TEST_F(CategoryDaoTest, FindByIdAndUuidRoundTrip) {
    auto c = MakeCategory(1);
    c.is_default = true;
    const auto id = dao_->Insert(c);
    ASSERT_TRUE(id.has_value());
    const auto by_id = dao_->FindById(*id);
    ASSERT_TRUE(by_id.has_value());
    EXPECT_EQ(by_id->uuid, c.uuid);
    EXPECT_EQ(by_id->name, c.name);
    EXPECT_EQ(by_id->color, c.color);
    EXPECT_TRUE(by_id->is_default);
    EXPECT_EQ(by_id->sort_order, c.sort_order);
    EXPECT_EQ(by_id->created_at, c.created_at);
    EXPECT_EQ(by_id->updated_at, c.updated_at);
    EXPECT_FALSE(by_id->is_deleted);

    const auto by_uuid = dao_->FindByUuid(c.uuid);
    ASSERT_TRUE(by_uuid.has_value());
    EXPECT_EQ(by_uuid->id, *id);
}

TEST_F(CategoryDaoTest, FindMissingReturnsNullopt) {
    EXPECT_FALSE(dao_->FindById(999).has_value());
    EXPECT_FALSE(dao_->FindByUuid(QStringLiteral("missing")).has_value());
}

TEST_F(CategoryDaoTest, ListActiveOrdersBySortOrderThenCreatedAt) {
    auto c1 = MakeCategory(1);
    c1.sort_order = 2;
    c1.created_at = 200;
    auto c2 = MakeCategory(2);
    c2.sort_order = 1;
    c2.created_at = 500;
    auto c3 = MakeCategory(3);
    c3.sort_order = 2;
    c3.created_at = 100;
    auto c4 = MakeCategory(4);
    c4.is_deleted = true;
    ASSERT_TRUE(dao_->Insert(c1).has_value());
    ASSERT_TRUE(dao_->Insert(c2).has_value());
    ASSERT_TRUE(dao_->Insert(c3).has_value());
    ASSERT_TRUE(dao_->Insert(c4).has_value());

    const auto list = dao_->ListActive();
    ASSERT_EQ(list.size(), 3u);
    EXPECT_EQ(list[0].uuid, c2.uuid);
    EXPECT_EQ(list[1].uuid, c3.uuid);
    EXPECT_EQ(list[2].uuid, c1.uuid);
}

TEST_F(CategoryDaoTest, ListIncludingDeletedReturnsAll) {
    auto c1 = MakeCategory(1);
    auto c2 = MakeCategory(2);
    c2.is_deleted = true;
    ASSERT_TRUE(dao_->Insert(c1).has_value());
    ASSERT_TRUE(dao_->Insert(c2).has_value());
    EXPECT_EQ(dao_->ListIncludingDeleted().size(), 2u);
}

TEST_F(CategoryDaoTest, UpdateChangesFields) {
    auto c = MakeCategory(1);
    const auto id = dao_->Insert(c);
    ASSERT_TRUE(id.has_value());
    c.id = *id;
    c.name = QStringLiteral("renamed");
    c.color = 0xdeadbeef;
    c.sort_order = 42;
    c.updated_at += 1;
    EXPECT_TRUE(dao_->Update(c));
    const auto reread = dao_->FindById(*id);
    ASSERT_TRUE(reread.has_value());
    EXPECT_EQ(reread->name, QStringLiteral("renamed"));
    EXPECT_EQ(reread->color, static_cast<int>(0xdeadbeef));
    EXPECT_EQ(reread->sort_order, 42);
}

TEST_F(CategoryDaoTest, HardDeleteRemovesRow) {
    const auto id = dao_->Insert(MakeCategory(1));
    ASSERT_TRUE(id.has_value());
    EXPECT_TRUE(dao_->HardDelete(*id));
    EXPECT_FALSE(dao_->FindById(*id).has_value());
    EXPECT_FALSE(dao_->HardDelete(*id));
}

TEST_F(CategoryDaoTest, DeleteAllCustomKeepsDefaults) {
    auto def = MakeCategory(1);
    def.is_default = true;
    auto custom_a = MakeCategory(2);
    auto custom_b = MakeCategory(3);
    ASSERT_TRUE(dao_->Insert(def).has_value());
    ASSERT_TRUE(dao_->Insert(custom_a).has_value());
    ASSERT_TRUE(dao_->Insert(custom_b).has_value());
    EXPECT_TRUE(dao_->DeleteAllCustom());
    const auto remaining = dao_->ListIncludingDeleted();
    ASSERT_EQ(remaining.size(), 1u);
    EXPECT_TRUE(remaining[0].is_default);
}

TEST_F(CategoryDaoTest, CountActiveExcludesTombstones) {
    auto c1 = MakeCategory(1);
    auto c2 = MakeCategory(2);
    auto c3 = MakeCategory(3);
    c3.is_deleted = true;
    ASSERT_TRUE(dao_->Insert(c1).has_value());
    ASSERT_TRUE(dao_->Insert(c2).has_value());
    ASSERT_TRUE(dao_->Insert(c3).has_value());
    EXPECT_EQ(dao_->CountActive(), 2);
}

TEST_F(CategoryDaoTest, UpdateSortOrderAndTimestamp) {
    const auto id = dao_->Insert(MakeCategory(1));
    ASSERT_TRUE(id.has_value());
    EXPECT_TRUE(dao_->UpdateSortOrder(*id, 99, 8888));
    const auto c = dao_->FindById(*id);
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->sort_order, 99);
    EXPECT_EQ(c->updated_at, 8888);
}

TEST_F(CategoryDaoTest, LogicalDeleteMarksAndTimestamps) {
    const auto id = dao_->Insert(MakeCategory(1));
    ASSERT_TRUE(id.has_value());
    EXPECT_TRUE(dao_->LogicalDelete(*id, 4321));
    const auto c = dao_->FindById(*id);
    ASSERT_TRUE(c.has_value());
    EXPECT_TRUE(c->is_deleted);
    EXPECT_EQ(c->updated_at, 4321);
}

TEST_F(CategoryDaoTest, DeleteOldTombstonesRespectsCutoff) {
    auto c_live = MakeCategory(1);
    auto c_old = MakeCategory(2);
    c_old.is_deleted = true;
    c_old.updated_at = 50;
    auto c_recent = MakeCategory(3);
    c_recent.is_deleted = true;
    c_recent.updated_at = 200;
    ASSERT_TRUE(dao_->Insert(c_live).has_value());
    ASSERT_TRUE(dao_->Insert(c_old).has_value());
    ASSERT_TRUE(dao_->Insert(c_recent).has_value());
    EXPECT_TRUE(dao_->DeleteOldTombstones(100));
    EXPECT_EQ(dao_->ListIncludingDeleted().size(), 2u);
}
