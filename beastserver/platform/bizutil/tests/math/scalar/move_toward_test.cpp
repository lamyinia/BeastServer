#include "beast/platform/bizutil/math/scalar/move_toward.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathScalarMoveTowardTest, StepsTowardTarget) {
    EXPECT_FLOAT_EQ(move_toward(0.f, 10.f, 3.f), 3.f);
    EXPECT_FLOAT_EQ(move_toward(10.f, 0.f, 3.f), 7.f);
    EXPECT_FLOAT_EQ(move_toward(0.f, 2.f, 5.f), 2.f);
}

} // namespace
} // namespace beast::platform::bizutil::math
