#include "beast/platform/bizutil/math/scalar/angle.hpp"
#include "beast/platform/bizutil/math/vector/vec2_ops.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathVectorTransformTest, RotateNinetyDegrees) {
    const Vec2f rotated = rotate(Vec2f{1.f, 0.f}, kHalfPi);
    EXPECT_NEAR(rotated.x, 0.f, 1e-5f);
    EXPECT_NEAR(rotated.y, 1.f, 1e-5f);
}

TEST(MathVectorTransformTest, AngleOfAndBetween) {
    EXPECT_NEAR(angle_of(Vec2f{0.f, 1.f}), kHalfPi, 1e-5f);
    EXPECT_NEAR(angle_between(Vec2f{1.f, 0.f}, Vec2f{0.f, 1.f}), kHalfPi, 1e-5f);
}

TEST(MathVectorTransformTest, ReflectAcrossVerticalWall) {
    const Vec2f reflected = reflect(Vec2f{1.f, -1.f}, Vec2f{0.f, 1.f});
    EXPECT_NEAR(reflected.x, 1.f, 1e-5f);
    EXPECT_NEAR(reflected.y, 1.f, 1e-5f);
}

TEST(MathVectorTransformTest, ProjectAndReject) {
    const Vec2f v{2.f, 3.f};
    const Vec2f onto{1.f, 0.f};
    EXPECT_NEAR(project(v, onto).x, 2.f, 1e-5f);
    EXPECT_NEAR(project(v, onto).y, 0.f, 1e-5f);
    EXPECT_NEAR(reject(v, onto).y, 3.f, 1e-5f);
}

TEST(MathVectorTransformTest, ClampLengthAndMoveToward) {
    const Vec2f clamped = clamp_length(Vec2f{3.f, 4.f}, 2.5f);
    EXPECT_NEAR(clamped.length(), 2.5f, 1e-5f);

    const Vec2f moved = move_toward(Vec2f{0.f, 0.f}, Vec2f{10.f, 0.f}, 4.f);
    EXPECT_NEAR(moved.x, 4.f, 1e-5f);

    const Vec2f mid = lerp(Vec2f{0.f, 0.f}, Vec2f{10.f, 20.f}, 0.5f);
    EXPECT_FLOAT_EQ(mid.x, 5.f);
    EXPECT_FLOAT_EQ(mid.y, 10.f);
}

} // namespace
} // namespace beast::platform::bizutil::math
