#include "beast/platform/bizutil/math/geometry/ray3.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathGeometryRay3Test, RayHitsAabb) {
    const Ray3f ray{{-5.f, 5.f, 5.f}, {1.f, 0.f, 0.f}};
    const Aabb3f box{{0.f, 0.f, 0.f}, {10.f, 10.f, 10.f}};
    const auto hit = intersect(ray, box);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(*hit, 5.f, 1e-4f);
}

TEST(MathGeometryRay3Test, RayMissesAabb) {
    const Ray3f ray{{-5.f, 20.f, 5.f}, {1.f, 0.f, 0.f}};
    const Aabb3f box{{0.f, 0.f, 0.f}, {10.f, 10.f, 10.f}};
    EXPECT_FALSE(intersect(ray, box).has_value());
}

TEST(MathGeometryRay3Test, RayHitsSphere) {
    const Ray3f ray{{-10.f, 0.f, 0.f}, {1.f, 0.f, 0.f}};
    const Spheref sphere{{0.f, 0.f, 0.f}, 3.f};
    const auto hit = intersect(ray, sphere);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(*hit, 7.f, 1e-4f);
}

TEST(MathGeometryRay3Test, RayMissesSphere) {
    const Ray3f ray{{-10.f, 10.f, 0.f}, {1.f, 0.f, 0.f}};
    const Spheref sphere{{0.f, 0.f, 0.f}, 3.f};
    EXPECT_FALSE(intersect(ray, sphere).has_value());
}

} // namespace
} // namespace beast::platform::bizutil::math
