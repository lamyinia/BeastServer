#include "beast/platform/bizutil/math/geometry/polygon.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathGeometryPolygonTest, ContainsPointInConcaveShape) {
    const std::vector<Vec2f> shape{
        {0.f, 0.f},
        {4.f, 0.f},
        {4.f, 4.f},
        {2.f, 2.f},
        {0.f, 4.f},
    };
    EXPECT_TRUE(contains(std::span<const Vec2f>{shape}, Vec2f{1.f, 1.f}));
    EXPECT_FALSE(contains(std::span<const Vec2f>{shape}, Vec2f{2.f, 3.f}));
    EXPECT_FALSE(contains(std::span<const Vec2f>{shape}, Vec2f{5.f, 5.f}));
}

} // namespace
} // namespace beast::platform::bizutil::math
