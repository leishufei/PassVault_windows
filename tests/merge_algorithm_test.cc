#include <gtest/gtest.h>

#include <QHash>
#include <QString>

#include <algorithm>
#include <cstdint>

#include "sync/merge_algorithm.h"

namespace {

struct Item {
    QString uuid;
    std::int64_t updated_at = 0;
    bool is_deleted = false;
    QString marker;
};

auto GetUpdatedAt = [](const Item& i) { return i.updated_at; };

QHash<QString, Item> MakeMap(std::initializer_list<Item> items) {
    QHash<QString, Item> map;
    for (const auto& it : items) map.insert(it.uuid, it);
    return map;
}

Item Find(const std::vector<Item>& v, const QString& uuid) {
    auto it = std::find_if(v.begin(), v.end(),
                           [&](const Item& x) { return x.uuid == uuid; });
    return it == v.end() ? Item{} : *it;
}

}  // namespace

TEST(MergeByUuid, LocalOnlyMarksLocalChanges) {
    auto local = MakeMap({{"a", 100, false, "L"}});
    QHash<QString, Item> cloud;
    auto result = passvault::sync::MergeByUuid(local, cloud, GetUpdatedAt);
    ASSERT_EQ(result.merged.size(), 1u);
    EXPECT_EQ(Find(result.merged, "a").marker, "L");
    EXPECT_TRUE(result.has_local_changes);
}

TEST(MergeByUuid, CloudOnlyKeepsCloudNoLocalChanges) {
    QHash<QString, Item> local;
    auto cloud = MakeMap({{"a", 100, false, "C"}});
    auto result = passvault::sync::MergeByUuid(local, cloud, GetUpdatedAt);
    ASSERT_EQ(result.merged.size(), 1u);
    EXPECT_EQ(Find(result.merged, "a").marker, "C");
    EXPECT_FALSE(result.has_local_changes);
}

TEST(MergeByUuid, EqualTimestampsPreferLocalNoUploadFlag) {
    auto local = MakeMap({{"a", 100, false, "L"}});
    auto cloud = MakeMap({{"a", 100, false, "C"}});
    auto result = passvault::sync::MergeByUuid(local, cloud, GetUpdatedAt);
    ASSERT_EQ(result.merged.size(), 1u);
    EXPECT_EQ(Find(result.merged, "a").marker, "L");
    EXPECT_FALSE(result.has_local_changes);
}

TEST(MergeByUuid, LocalNewerWinsAndFlagsChanges) {
    auto local = MakeMap({{"a", 200, false, "L"}});
    auto cloud = MakeMap({{"a", 100, false, "C"}});
    auto result = passvault::sync::MergeByUuid(local, cloud, GetUpdatedAt);
    ASSERT_EQ(result.merged.size(), 1u);
    EXPECT_EQ(Find(result.merged, "a").marker, "L");
    EXPECT_TRUE(result.has_local_changes);
}

TEST(MergeByUuid, CloudNewerWinsNoLocalChanges) {
    auto local = MakeMap({{"a", 100, false, "L"}});
    auto cloud = MakeMap({{"a", 200, false, "C"}});
    auto result = passvault::sync::MergeByUuid(local, cloud, GetUpdatedAt);
    ASSERT_EQ(result.merged.size(), 1u);
    EXPECT_EQ(Find(result.merged, "a").marker, "C");
    EXPECT_FALSE(result.has_local_changes);
}

TEST(MergeByUuid, TombstoneOnLocalPropagates) {
    auto local = MakeMap({{"a", 200, true, "L-del"}});
    auto cloud = MakeMap({{"a", 100, false, "C-live"}});
    auto result = passvault::sync::MergeByUuid(local, cloud, GetUpdatedAt);
    ASSERT_EQ(result.merged.size(), 1u);
    auto item = Find(result.merged, "a");
    EXPECT_EQ(item.marker, "L-del");
    EXPECT_TRUE(item.is_deleted);
    EXPECT_TRUE(result.has_local_changes);
}

TEST(MergeByUuid, TombstoneOnCloudPropagates) {
    auto local = MakeMap({{"a", 100, false, "L-live"}});
    auto cloud = MakeMap({{"a", 200, true, "C-del"}});
    auto result = passvault::sync::MergeByUuid(local, cloud, GetUpdatedAt);
    ASSERT_EQ(result.merged.size(), 1u);
    auto item = Find(result.merged, "a");
    EXPECT_EQ(item.marker, "C-del");
    EXPECT_TRUE(item.is_deleted);
    EXPECT_FALSE(result.has_local_changes);
}

TEST(MergeByUuid, BothTombstonedNewerWins) {
    auto local = MakeMap({{"a", 300, true, "L"}});
    auto cloud = MakeMap({{"a", 200, true, "C"}});
    auto result = passvault::sync::MergeByUuid(local, cloud, GetUpdatedAt);
    ASSERT_EQ(result.merged.size(), 1u);
    EXPECT_EQ(Find(result.merged, "a").marker, "L");
    EXPECT_TRUE(result.has_local_changes);
}

TEST(MergeByUuid, MixedMapCombinesAllUuids) {
    auto local = MakeMap({
        {"a", 200, false, "L-a"},   // local newer
        {"b", 100, false, "L-b"},   // local only
    });
    auto cloud = MakeMap({
        {"a", 100, false, "C-a"},
        {"c", 100, false, "C-c"},   // cloud only
    });
    auto result = passvault::sync::MergeByUuid(local, cloud, GetUpdatedAt);
    ASSERT_EQ(result.merged.size(), 3u);
    EXPECT_EQ(Find(result.merged, "a").marker, "L-a");
    EXPECT_EQ(Find(result.merged, "b").marker, "L-b");
    EXPECT_EQ(Find(result.merged, "c").marker, "C-c");
    EXPECT_TRUE(result.has_local_changes);
}

TEST(MergeByUuid, EmptyMapsProduceEmpty) {
    QHash<QString, Item> local;
    QHash<QString, Item> cloud;
    auto result = passvault::sync::MergeByUuid(local, cloud, GetUpdatedAt);
    EXPECT_TRUE(result.merged.empty());
    EXPECT_FALSE(result.has_local_changes);
}
