#include "beast/mixin/dirtypersist/entity_traits.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

// 测试用实体（必须放在文件作用域，不能在匿名命名空间内
// —— 否则 EntityTraits 特化会被嵌套到匿名命名空间里，concept 查不到）
struct PlayerEntity {
    std::string  uid;
    std::int64_t hp;
    std::int64_t gold;
    std::string  name;
};

// 声明式注册 EntityTraits<PlayerEntity>
// C++ 要求特化必须放在被特化模板所属的命名空间中：
// 打开 beast::platform::dirtypersist 命名空间（必须在文件作用域打开，
// 不能在匿名命名空间内打开，否则实际命名空间是 anon::beast::platform::dirtypersist）。
namespace beast::platform::dirtypersist {
template<>
struct EntityTraits<::PlayerEntity> {
    using entity_type = ::PlayerEntity;
    static constexpr std::string_view kTable    = "players";
    static constexpr std::string_view kIdColumn = "uid";
    static constexpr std::size_t      kIdOffset = offsetof(::PlayerEntity, uid);
    static constexpr FieldType        kIdType   = FieldType::String;
    static constexpr FieldMeta        kFields[] = {
        BEAST_PERSIST_FIELD(hp,   Int64),
        BEAST_PERSIST_FIELD(gold, Int64),
        BEAST_PERSIST_FIELD(name, String),
    };
};
} // namespace beast::platform::dirtypersist

static_assert(beast::platform::dirtypersist::Persistable<PlayerEntity>,
              "PlayerEntity must satisfy Persistable concept");

namespace {

using beast::platform::dirtypersist::FieldValue;
using beast::platform::dirtypersist::FieldType;
using beast::platform::dirtypersist::find_field_idx;
using beast::platform::dirtypersist::diff;
using beast::platform::dirtypersist::flatten;
using beast::platform::dirtypersist::get_field;
using beast::platform::dirtypersist::get_id;
using beast::platform::dirtypersist::set_field;

TEST(EntityTraitsTest, GetFieldByIndex) {
    PlayerEntity e{.uid="u1", .hp=50, .gold=1000, .name="alice"};
    EXPECT_EQ(std::get<std::int64_t>(get_field(e, 0)), 50);   // hp
    EXPECT_EQ(std::get<std::int64_t>(get_field(e, 1)), 1000);  // gold
    EXPECT_EQ(std::get<std::string>(get_field(e, 2)), "alice");  // name
}

TEST(EntityTraitsTest, SetFieldByIndex) {
    PlayerEntity e{.uid="u1", .hp=50, .gold=1000, .name="alice"};
    set_field(e, 0, FieldValue{std::int64_t{80}});
    set_field(e, 2, FieldValue{std::string{"bob"}});
    EXPECT_EQ(e.hp, 80);
    EXPECT_EQ(e.name, "bob");
}

TEST(EntityTraitsTest, GetIdAsString) {
    PlayerEntity e{.uid="uid-abc", .hp=50, .gold=1000, .name="alice"};
    EXPECT_EQ(get_id(e), "uid-abc");
}

TEST(EntityTraitsTest, FlattenAllFields) {
    PlayerEntity e{.uid="uid-abc", .hp=50, .gold=1000, .name="alice"};
    auto fields = flatten(e);
    EXPECT_EQ(fields.size(), 3u);
    EXPECT_EQ(fields[0].first, "hp");
    EXPECT_EQ(fields[1].first, "gold");
    EXPECT_EQ(fields[2].first, "name");
}

TEST(EntityTraitsTest, DiffReturnsChangedFields) {
    PlayerEntity old_v{.uid="u1", .hp=50,  .gold=1000, .name="alice"};
    PlayerEntity new_v{.uid="u1", .hp=80,  .gold=1000, .name="bob"};

    std::vector<std::pair<std::size_t, FieldValue>> out;
    diff(old_v, new_v, out);

    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].first, 0u);  // hp changed
    EXPECT_EQ(std::get<std::int64_t>(out[0].second), 80);
    EXPECT_EQ(out[1].first, 2u);  // name changed
    EXPECT_EQ(std::get<std::string>(out[1].second), "bob");
}

TEST(EntityTraitsTest, DiffNoChangeReturnsEmpty) {
    PlayerEntity a{.uid="u1", .hp=50, .gold=1000, .name="alice"};
    PlayerEntity b{.uid="u1", .hp=50, .gold=1000, .name="alice"};
    std::vector<std::pair<std::size_t, FieldValue>> out;
    diff(a, b, out);
    EXPECT_TRUE(out.empty());
}

TEST(EntityTraitsTest, FindFieldIdx) {
    EXPECT_EQ(find_field_idx<PlayerEntity>("hp"), 0u);
    EXPECT_EQ(find_field_idx<PlayerEntity>("gold"), 1u);
    EXPECT_EQ(find_field_idx<PlayerEntity>("name"), 2u);
    EXPECT_EQ(find_field_idx<PlayerEntity>("unknown"), static_cast<std::size_t>(-1));
}

} // namespace
