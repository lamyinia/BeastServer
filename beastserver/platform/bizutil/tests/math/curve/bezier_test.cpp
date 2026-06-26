#include "beast/platform/bizutil/math/curve/bezier.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathCurveBezierTest, QuadraticEndpoints) {
    const Vec2f p0{0.f, 0.f};
    const Vec2f p1{5.f, 10.f};
    const Vec2f p2{10.f, 0.f};
    EXPECT_EQ(bezier_quadratic(p0, p1, p2, 0.f), p0);
    EXPECT_EQ(bezier_quadratic(p0, p1, p2, 1.f), p2);
    EXPECT_FLOAT_EQ(bezier_quadratic(p0, p1, p2, 0.5f).x, 5.f);
}

TEST(MathCurveBezierTest, CubicEndpoints) {
    const Vec2f p0{0.f, 0.f};
    const Vec2f p3{10.f, 10.f};
    EXPECT_EQ(bezier_cubic(p0, {3.f, 0.f}, {7.f, 10.f}, p3, 0.f), p0);
    EXPECT_EQ(bezier_cubic(p0, {3.f, 0.f}, {7.f, 10.f}, p3, 1.f), p3);
}

} // namespace
} // namespace beast::platform::bizutil::math
