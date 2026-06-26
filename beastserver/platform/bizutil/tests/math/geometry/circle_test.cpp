#include "beast/platform/bizutil/math/geometry/circle.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathGeometryCircleTest, ContainsAndIntersects) {
    const Circlef unit{{0.f, 0.f}, 5.f};
    EXPECT_TRUE(contains(unit, Vec2f{3.f, 4.f}));
    EXPECT_FALSE(contains(unit, Vec2f{6.f, 0.f}));

    const Circlef other{{8.f, 0.f}, 5.f};
    EXPECT_TRUE(intersects(unit, other));
    EXPECT_FALSE(intersects(unit, Circlef{{20.f, 0.f}, 1.f}));
}

} // namespace
} // namespace beast::platform::bizutil::math
