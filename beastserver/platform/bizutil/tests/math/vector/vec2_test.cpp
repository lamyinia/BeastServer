#include "beast/platform/bizutil/math/vector/vec2.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathVectorVec2Test, IntegerAdditionAndSubtraction) {
    const Vec2i a{1, 2};
    const Vec2i b{3, 4};
    EXPECT_EQ(a + b, Vec2i(4, 6));
    EXPECT_EQ(b - a, Vec2i(2, 2));
}

TEST(MathVectorVec2Test, FloatLengthSquared) {
    const Vec2f v{3.f, 4.f};
    EXPECT_FLOAT_EQ(v.length_squared(), 25.f);
    EXPECT_FLOAT_EQ(v.length(), 5.f);
}

} // namespace
} // namespace beast::platform::bizutil::math
