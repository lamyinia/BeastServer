#include "beast/platform/bizutil/math/geometry/capsule.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathGeometryCapsuleTest, ContainsPointNearAxis) {
    const Capsulef cap{Segmentf{{0.f, 0.f}, {10.f, 0.f}}, 2.f};
    EXPECT_TRUE(contains(cap, Vec2f{5.f, 1.f}));
    EXPECT_TRUE(contains(cap, Vec2f{-1.f, 0.f}));
    EXPECT_FALSE(contains(cap, Vec2f{5.f, 3.f}));
}

} // namespace
} // namespace beast::platform::bizutil::math
