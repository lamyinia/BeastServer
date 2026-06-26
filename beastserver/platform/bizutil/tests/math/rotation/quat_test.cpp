#include "beast/platform/bizutil/math/rotation/quat.hpp"
#include "beast/platform/bizutil/math/scalar/angle.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathRotationQuatTest, IdentityRotationKeepsVector) {
    const Vec3f v{1.f, 2.f, 3.f};
    EXPECT_EQ(rotate(identity_quat(), v), v);
}

TEST(MathRotationQuatTest, AxisAngleRotatesVector) {
    const Quatf q = from_axis_angle({0.f, 0.f, 1.f}, kHalfPi);
    const Vec3f rotated = rotate(q, {1.f, 0.f, 0.f});

    EXPECT_NEAR(rotated.x, 0.f, 1e-5f);
    EXPECT_NEAR(rotated.y, 1.f, 1e-5f);
    EXPECT_NEAR(rotated.z, 0.f, 1e-5f);
}

TEST(MathRotationQuatTest, MultiplicationComposesRotations) {
    const Quatf yaw_z = from_axis_angle({0.f, 0.f, 1.f}, kHalfPi);
    const Quatf pitch_y = from_axis_angle({0.f, 1.f, 0.f}, kHalfPi);
    const Vec3f rotated = rotate(pitch_y * yaw_z, {1.f, 0.f, 0.f});

    EXPECT_NEAR(rotated.x, 0.f, 1e-5f);
    EXPECT_NEAR(rotated.y, 1.f, 1e-5f);
    EXPECT_NEAR(rotated.z, 0.f, 1e-5f);
}

TEST(MathRotationQuatTest, InverseUndoRotation) {
    const Quatf q = from_axis_angle({0.f, 0.f, 1.f}, kHalfPi);
    const Vec3f v{1.f, 0.f, 0.f};

    const Vec3f restored = rotate(inverse(q), rotate(q, v));
    EXPECT_NEAR(restored.x, v.x, 1e-5f);
    EXPECT_NEAR(restored.y, v.y, 1e-5f);
    EXPECT_NEAR(restored.z, v.z, 1e-5f);
}

TEST(MathRotationQuatTest, SlerpHalfwayRotation) {
    const Quatf from = identity_quat();
    const Quatf to = from_axis_angle({0.f, 0.f, 1.f}, kHalfPi);
    const Quatf half = slerp(from, to, 0.5f);
    const Vec3f rotated = rotate(half, {1.f, 0.f, 0.f});

    EXPECT_NEAR(rotated.x, 0.7071067f, 1e-5f);
    EXPECT_NEAR(rotated.y, 0.7071067f, 1e-5f);
    EXPECT_NEAR(rotated.z, 0.f, 1e-5f);
    EXPECT_NEAR(half.length(), 1.f, 1e-5f);
}

TEST(MathRotationQuatTest, FromYawYRotatesAroundYAxis) {
    const Vec3f rotated = rotate(from_yaw_y(kHalfPi), {0.f, 0.f, 1.f});

    EXPECT_NEAR(rotated.x, 1.f, 1e-5f);
    EXPECT_NEAR(rotated.y, 0.f, 1e-5f);
    EXPECT_NEAR(rotated.z, 0.f, 1e-5f);
}

} // namespace
} // namespace beast::platform::bizutil::math
