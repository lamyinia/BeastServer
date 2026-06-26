#include "beast/platform/bizutil/math/geometry/ring.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathGeometryRingTest, ContainsBetweenRadii) {
    const Ringf ring{{0.f, 0.f}, 2.f, 5.f};
    EXPECT_TRUE(contains(ring, Vec2f{3.f, 0.f}));
    EXPECT_FALSE(contains(ring, Vec2f{1.f, 0.f}));
    EXPECT_FALSE(contains(ring, Vec2f{6.f, 0.f}));
}

} // namespace
} // namespace beast::platform::bizutil::math
