#include "beast/platform/bizutil/math/geometry/aabb3.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathGeometryAabb3Test, ContainsAndIntersects) {
    const Aabb3f box{{0.f, 0.f, 0.f}, {10.f, 10.f, 10.f}};
    EXPECT_TRUE(contains(box, Vec3f{5.f, 5.f, 5.f}));
    EXPECT_FALSE(contains(box, Vec3f{11.f, 5.f, 5.f}));

    EXPECT_TRUE(intersects(box, Aabb3f{{5.f, 5.f, 5.f}, {15.f, 15.f, 15.f}}));
    EXPECT_FALSE(intersects(box, Aabb3f{{20.f, 20.f, 20.f}, {30.f, 30.f, 30.f}}));
    EXPECT_EQ(box.center(), Vec3f(5.f, 5.f, 5.f));
}

} // namespace
} // namespace beast::platform::bizutil::math
