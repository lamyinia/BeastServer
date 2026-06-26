#include "beast/platform/bizutil/math/scalar/wrap.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathScalarWrapTest, WrapsIntoHalfOpenRange) {
    EXPECT_EQ(wrap(0, 0, 10), 0);
    EXPECT_EQ(wrap(10, 0, 10), 0);
    EXPECT_EQ(wrap(11, 0, 10), 1);
    EXPECT_EQ(wrap(-1, 0, 10), 9);
}

} // namespace
} // namespace beast::platform::bizutil::math
