#include "beast/mixin/dirtypersist/dirty_tracker.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

using beast::platform::dirtypersist::DirtyTracker;
using beast::platform::dirtypersist::FieldValue;
using beast::platform::dirtypersist::FlushOp;

TEST(DirtyTrackerTest, MarkFieldDirtyCreatesEntry) {
    DirtyTracker t;
    t.mark_field_dirty("players", "uid1", "hp", std::int64_t{50});
    EXPECT_FALSE(t.empty());
    EXPECT_EQ(t.size(), 1u);
    EXPECT_EQ(t.field_count(), 1u);
}

TEST(DirtyTrackerTest, SameFieldOverwritesOldValue) {
    DirtyTracker t;
    t.mark_field_dirty("players", "uid1", "hp", std::int64_t{50});
    t.mark_field_dirty("players", "uid1", "hp", std::int64_t{80});

    auto batch = t.take_dirty();
    ASSERT_EQ(batch.size(), 1u);
    EXPECT_EQ(batch[0].fields.size(), 1u);
    EXPECT_EQ(batch[0].fields[0].first, "hp");
    EXPECT_EQ(std::get<std::int64_t>(batch[0].fields[0].second), 80);
}

TEST(DirtyTrackerTest, DifferentFieldsAccumulate) {
    DirtyTracker t;
    t.mark_field_dirty("players", "uid1", "hp",   std::int64_t{50});
    t.mark_field_dirty("players", "uid1", "gold", std::int64_t{1000});
    t.mark_field_dirty("players", "uid2", "hp",   std::int64_t{60});

    EXPECT_EQ(t.size(), 2u);
    EXPECT_EQ(t.field_count(), 3u);

    auto batch = t.take_dirty();
    EXPECT_EQ(batch.size(), 2u);
    EXPECT_TRUE(t.empty());
}

TEST(DirtyTrackerTest, TakeOneExtractsSingleEntity) {
    DirtyTracker t;
    t.mark_field_dirty("players", "uid1", "hp", std::int64_t{50});
    t.mark_field_dirty("players", "uid2", "hp", std::int64_t{60});

    auto one = t.take_one("players", "uid1");
    ASSERT_TRUE(one.has_value());
    EXPECT_EQ(one->id, "uid1");
    EXPECT_EQ(one->fields.size(), 1u);

    EXPECT_EQ(t.size(), 1u);  // uid2 还在
    auto none = t.take_one("players", "uid1");
    EXPECT_FALSE(none.has_value());
}

TEST(DirtyTrackerTest, MarkEntityDirtyReplacesFields) {
    DirtyTracker t;
    t.mark_field_dirty("players", "uid1", "hp", std::int64_t{50});

    // 用 mark_entity_dirty 覆盖
    std::vector<std::pair<std::string, FieldValue>> fields = {
        {"hp",   std::int64_t{100}},
        {"gold", std::int64_t{5000}},
    };
    t.mark_entity_dirty("players", "uid1", std::move(fields));

    auto batch = t.take_dirty();
    ASSERT_EQ(batch.size(), 1u);
    EXPECT_EQ(batch[0].fields.size(), 2u);
}

TEST(DirtyTrackerTest, ClearRemovesAll) {
    DirtyTracker t;
    t.mark_field_dirty("players", "uid1", "hp", std::int64_t{50});
    EXPECT_FALSE(t.empty());

    t.clear();
    EXPECT_TRUE(t.empty());
    EXPECT_EQ(t.field_count(), 0u);
}

} // namespace
