#include "beast/platform/bizutil/math/algorithm/line.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathAlgorithmLineTest, BresenhamIncludesEndpoints) {
    const auto line = bresenham_line({0, 0}, {3, 0});
    ASSERT_EQ(line.size(), 4U);
    EXPECT_EQ(line.front(), Vec2i(0, 0));
    EXPECT_EQ(line.back(), Vec2i(3, 0));
}

} // namespace
} // namespace beast::platform::bizutil::math
