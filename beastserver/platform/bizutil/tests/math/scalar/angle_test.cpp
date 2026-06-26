#include "beast/platform/bizutil/math/scalar/angle.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathScalarAngleTest, DegreeRadianConversion) {
    EXPECT_NEAR(deg_to_rad(180.f), kPi, 1e-5f);
    EXPECT_NEAR(rad_to_deg(kPi), 180.f, 1e-3f);
    EXPECT_NEAR(deg_to_rad(90.f), kHalfPi, 1e-5f);
}

} // namespace
} // namespace beast::platform::bizutil::math
