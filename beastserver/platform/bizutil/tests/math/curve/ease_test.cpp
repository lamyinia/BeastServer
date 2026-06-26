#include "beast/platform/bizutil/math/curve/ease.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathCurveEaseTest, EndpointsAndMidpoint) {
    EXPECT_FLOAT_EQ(smoothstep(0.f), 0.f);
    EXPECT_FLOAT_EQ(smoothstep(1.f), 1.f);
    EXPECT_FLOAT_EQ(smoothstep(0.5f), 0.5f);
    EXPECT_FLOAT_EQ(ease_in_quad(0.5f), 0.25f);
    EXPECT_FLOAT_EQ(smootherstep(0.f), 0.f);
    EXPECT_FLOAT_EQ(smootherstep(1.f), 1.f);
}

} // namespace
} // namespace beast::platform::bizutil::math
