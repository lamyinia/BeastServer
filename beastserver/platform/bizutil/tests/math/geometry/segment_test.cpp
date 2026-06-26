#include "beast/platform/bizutil/math/geometry/segment.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathGeometrySegmentTest, DistanceAndContains) {
    const Segmentf segment{{0.f, 0.f}, {10.f, 0.f}};
    EXPECT_FLOAT_EQ(distance(segment, Vec2f{5.f, 0.f}), 0.f);
    EXPECT_FLOAT_EQ(distance(segment, Vec2f{5.f, 3.f}), 3.f);
    EXPECT_TRUE(contains(segment, Vec2f{2.f, 0.f}));
    EXPECT_FALSE(contains(segment, Vec2f{2.f, 1.f}));
}

} // namespace
} // namespace beast::platform::bizutil::math
