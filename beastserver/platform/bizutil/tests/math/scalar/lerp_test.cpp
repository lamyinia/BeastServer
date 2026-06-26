#include "beast/platform/bizutil/math/scalar/lerp.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathScalarLerpTest, InterpolatesBetweenValues) {
    EXPECT_FLOAT_EQ(lerp(0.f, 10.f, 0.f), 0.f);
    EXPECT_FLOAT_EQ(lerp(0.f, 10.f, 1.f), 10.f);
    EXPECT_FLOAT_EQ(lerp(0.f, 10.f, 0.5f), 5.f);
}

} // namespace
} // namespace beast::platform::bizutil::math
