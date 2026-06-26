#include "beast/platform/bizutil/math/random/distribution.hpp"
#include "beast/platform/bizutil/math/vector/vec2_ops.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathRandomDistributionTest, UnitVecHasUnitLength) {
    SeededRng rng(1);
    for (int i = 0; i < 16; ++i) {
        EXPECT_NEAR(random_unit_vec2(rng).length(), 1.f, 1e-4f);
    }
}

TEST(MathRandomDistributionTest, PointInCircleStaysWithinRadius) {
    SeededRng rng(2);
    const Vec2f center{10.f, 10.f};
    for (int i = 0; i < 64; ++i) {
        EXPECT_LE(distance(center, random_point_in_circle(rng, center, 5.f)), 5.f + 1e-4f);
    }
}

TEST(MathRandomDistributionTest, IsDeterministic) {
    SeededRng a(42);
    SeededRng b(42);
    EXPECT_EQ(random_point_in_circle(a, {0.f, 0.f}, 3.f), random_point_in_circle(b, {0.f, 0.f}, 3.f));
}

} // namespace
} // namespace beast::platform::bizutil::math
