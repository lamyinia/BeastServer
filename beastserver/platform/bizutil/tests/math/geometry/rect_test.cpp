#include "beast/platform/bizutil/math/geometry/rect.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathGeometryRectTest, ContainsPointHalfOpenInterval) {
    const Recti attack{10, 20, 3, 3};

    EXPECT_TRUE(contains(attack, Vec2i{10, 20}));
    EXPECT_TRUE(contains(attack, Vec2i{12, 22}));
    EXPECT_FALSE(contains(attack, Vec2i{13, 22}));
    EXPECT_FALSE(contains(attack, Vec2i{9, 20}));
}

TEST(MathGeometryRectTest, IntersectsOverlappingRects) {
    const Recti a{0, 0, 4, 4};
    const Recti b{3, 3, 4, 4};
    const Recti c{10, 10, 2, 2};

    EXPECT_TRUE(intersects(a, b));
    EXPECT_FALSE(intersects(a, c));
}

TEST(MathGeometryRectTest, FloatRectContainsPoint) {
    const Rectf area{0.f, 0.f, 10.f, 10.f};
    EXPECT_TRUE(contains(area, Vec2f{5.f, 5.f}));
    EXPECT_FALSE(contains(area, Vec2f{10.f, 5.f}));
}

} // namespace
} // namespace beast::platform::bizutil::math
