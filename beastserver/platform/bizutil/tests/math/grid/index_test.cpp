#include "beast/platform/bizutil/math/grid/index.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathGridIndexTest, ToIndexAndFromIndex) {
    const Vec2i point{3, 4};
    const int width = 10;

    EXPECT_EQ(to_index(point, width), 43);
    EXPECT_EQ(from_index(43, width), point);
}

} // namespace
} // namespace beast::platform::bizutil::math
