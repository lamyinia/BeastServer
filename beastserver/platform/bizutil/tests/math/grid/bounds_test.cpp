#include "beast/platform/bizutil/math/grid/bounds.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathGridBoundsTest, InBounds) {
    EXPECT_TRUE(in_bounds(Vec2i{0, 0}, 100, 100));
    EXPECT_FALSE(in_bounds(Vec2i{100, 0}, 100, 100));
}

TEST(MathGridBoundsTest, ClampToBounds) {
    EXPECT_EQ(clamp_to_bounds(Vec2i{-1, 50}, 100, 100), Vec2i(0, 50));
    EXPECT_EQ(clamp_to_bounds(Vec2i{150, 150}, 100, 100), Vec2i(99, 99));
}

} // namespace
} // namespace beast::platform::bizutil::math
