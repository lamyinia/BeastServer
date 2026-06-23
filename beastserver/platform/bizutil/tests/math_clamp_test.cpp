#include "beast/platform/bizutil/math/clamp.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathClampTest, ClampsValueIntoRange) {
    EXPECT_EQ(clamp(5, 1, 10), 5);
    EXPECT_EQ(clamp(0, 1, 10), 1);
    EXPECT_EQ(clamp(99, 1, 10), 10);
}

} // namespace
} // namespace beast::platform::bizutil::math
