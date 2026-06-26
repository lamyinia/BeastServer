#include "beast/platform/bizutil/math/scalar/approx.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathScalarApproxTest, EqualWithinEpsilon) {
    EXPECT_TRUE(approx_equal(1.0f, 1.0f + 1e-6f));
    EXPECT_FALSE(approx_equal(1.0f, 1.1f));
    EXPECT_TRUE(approx_zero(1e-6f));
    EXPECT_FALSE(approx_zero(0.5f));
}

} // namespace
} // namespace beast::platform::bizutil::math
