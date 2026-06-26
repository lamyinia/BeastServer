#include "beast/platform/bizutil/math/math.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathUmbrellaTest, IncludesP0Headers) {
    EXPECT_TRUE(in_bounds(Vec2i{0, 0}, 10, 10));
    EXPECT_TRUE(contains(Recti{0, 0, 5, 5}, Vec2i{2, 2}));
    EXPECT_EQ(clamp(99, 0, 10), 10);
    EXPECT_EQ(manhattan(Vec2i{0, 0}, Vec2i{1, 1}), 2);
    EXPECT_EQ(neighbors4(Vec2i{0, 0}).size(), 4U);
    EXPECT_EQ(to_index(Vec2i{1, 2}, 5), 11);
    SeededRng rng(1);
    EXPECT_GE(rng.uniform_int(0, 3), 0);
}

} // namespace
} // namespace beast::platform::bizutil::math
