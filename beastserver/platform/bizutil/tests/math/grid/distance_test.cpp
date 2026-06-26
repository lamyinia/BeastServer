#include "beast/platform/bizutil/math/grid/distance.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathGridDistanceTest, ManhattanAndChebyshev) {
    EXPECT_EQ(manhattan(Vec2i{0, 0}, Vec2i{3, 4}), 7);
    EXPECT_EQ(chebyshev(Vec2i{0, 0}, Vec2i{3, 4}), 4);
}

} // namespace
} // namespace beast::platform::bizutil::math
