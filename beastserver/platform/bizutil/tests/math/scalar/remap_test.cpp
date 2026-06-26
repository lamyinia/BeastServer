#include "beast/platform/bizutil/math/scalar/remap.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathScalarRemapTest, MapsBetweenRanges) {
    EXPECT_FLOAT_EQ(remap(5.f, 0.f, 10.f, 0.f, 100.f), 50.f);
    EXPECT_FLOAT_EQ(remap(0.f, 0.f, 10.f, 20.f, 40.f), 20.f);
    EXPECT_FLOAT_EQ(remap(10.f, 0.f, 10.f, 20.f, 40.f), 40.f);
    EXPECT_FLOAT_EQ(remap(5.f, 5.f, 5.f, 1.f, 9.f), 1.f);
}

} // namespace
} // namespace beast::platform::bizutil::math
