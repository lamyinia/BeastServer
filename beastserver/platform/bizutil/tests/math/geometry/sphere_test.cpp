#include "beast/platform/bizutil/math/geometry/sphere.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathGeometrySphereTest, ContainsAndSphereSphere) {
    const Spheref s{{0.f, 0.f, 0.f}, 5.f};
    EXPECT_TRUE(contains(s, Vec3f{3.f, 0.f, 4.f}));
    EXPECT_FALSE(contains(s, Vec3f{4.f, 4.f, 4.f}));

    EXPECT_TRUE(intersects(s, Spheref{{8.f, 0.f, 0.f}, 5.f}));
    EXPECT_FALSE(intersects(s, Spheref{{20.f, 0.f, 0.f}, 1.f}));
}

TEST(MathGeometrySphereTest, SphereVsAabb) {
    const Aabb3f box{{0.f, 0.f, 0.f}, {10.f, 10.f, 10.f}};
    EXPECT_TRUE(intersects(Spheref{{-1.f, 5.f, 5.f}, 2.f}, box));
    EXPECT_FALSE(intersects(Spheref{{-5.f, 5.f, 5.f}, 2.f}, box));
    EXPECT_TRUE(intersects(box, Spheref{{5.f, 5.f, 5.f}, 1.f}));
}

} // namespace
} // namespace beast::platform::bizutil::math
