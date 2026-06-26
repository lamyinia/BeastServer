#include "beast/platform/bizutil/math/grid/neighbors.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathGridNeighborsTest, Neighbors4And8) {
    const auto n4 = neighbors4(Vec2i{5, 5});
    EXPECT_EQ(n4[0], Vec2i(5, 4));
    EXPECT_EQ(n4[2], Vec2i(5, 6));

    const auto n8 = neighbors8(Vec2i{1, 1});
    EXPECT_EQ(n8.size(), 8U);
}

} // namespace
} // namespace beast::platform::bizutil::math
