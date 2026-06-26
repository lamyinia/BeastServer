#include "beast/platform/bizutil/math/spatial/hash_grid.hpp"
#include "beast/platform/bizutil/math/spatial/query_radius.hpp"

#include <gtest/gtest.h>

#include <string>
#include <unordered_map>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathSpatialHashGridTest, QueryRadiusFindsNearbyEntities) {
    HashGrid<int> grid(2.f);
    grid.insert(1, {0.f, 0.f});
    grid.insert(2, {1.f, 0.f});
    grid.insert(3, {10.f, 10.f});

    const auto nearby = query_radius(grid, Vec2f{0.f, 0.f}, 2.f);
    EXPECT_EQ(nearby.size(), 2U);

    grid.update(2, {10.f, 10.f});
    const auto updated = grid.query_radius({0.f, 0.f}, 2.f);
    EXPECT_EQ(updated.size(), 1U);
}

TEST(MathSpatialQueryRadiusTest, BruteForceMatchesExpectation) {
    const std::vector<int> ids{1, 2, 3};
    const std::unordered_map<int, Vec2f> positions{
        {1, {0.f, 0.f}},
        {2, {1.5f, 0.f}},
        {3, {8.f, 0.f}},
    };

    const auto found = query_radius_brute<int>(
        ids,
        [&positions](const int id) { return positions.at(id); },
        Vec2f{0.f, 0.f},
        2.f);
    EXPECT_EQ(found.size(), 2U);
}

} // namespace
} // namespace beast::platform::bizutil::math
