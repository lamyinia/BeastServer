#include "beast/platform/bizutil/math/geometry/intersect.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathGeometryIntersectTest, CircleVsRect) {
    const Rectf rect{0.f, 0.f, 10.f, 10.f};
    EXPECT_TRUE(intersects(Circlef{{5.f, 5.f}, 2.f}, rect));
    EXPECT_TRUE(intersects(Circlef{{-1.f, 5.f}, 2.f}, rect));
    EXPECT_FALSE(intersects(Circlef{{20.f, 20.f}, 2.f}, rect));
    EXPECT_TRUE(intersects(rect, Circlef{{5.f, 5.f}, 1.f}));
}

TEST(MathGeometryIntersectTest, CircleVsSegment) {
    const Segmentf segment{{0.f, 0.f}, {10.f, 0.f}};
    EXPECT_TRUE(intersects(Circlef{{5.f, 1.f}, 2.f}, segment));
    EXPECT_FALSE(intersects(Circlef{{5.f, 5.f}, 2.f}, segment));
}

TEST(MathGeometryIntersectTest, SegmentVsSegment) {
    const Segmentf a{{0.f, 0.f}, {10.f, 10.f}};
    const Segmentf b{{0.f, 10.f}, {10.f, 0.f}};
    const Segmentf c{{0.f, 5.f}, {1.f, 5.f}};
    EXPECT_TRUE(intersects(a, b));
    EXPECT_FALSE(intersects(a, c));
}

} // namespace
} // namespace beast::platform::bizutil::math
