#include "beast/platform/bizutil/math/vector/vec3_ops.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathVectorVec3OpsTest, DotAndCross) {
    EXPECT_FLOAT_EQ(dot(Vec3f{1.f, 2.f, 3.f}, Vec3f{4.f, 5.f, 6.f}), 32.f);

    const Vec3f c = cross(Vec3f{1.f, 0.f, 0.f}, Vec3f{0.f, 1.f, 0.f});
    EXPECT_EQ(c, Vec3f(0.f, 0.f, 1.f));
}

TEST(MathVectorVec3OpsTest, NormalizeDistanceLerp) {
    EXPECT_NEAR(normalize(Vec3f{0.f, 0.f, 5.f}).z, 1.f, 1e-5f);
    EXPECT_FLOAT_EQ(distance(Vec3f{0.f, 0.f, 0.f}, Vec3f{0.f, 3.f, 4.f}), 5.f);

    const Vec3f mid = lerp(Vec3f{0.f, 0.f, 0.f}, Vec3f{10.f, 20.f, 30.f}, 0.5f);
    EXPECT_EQ(mid, Vec3f(5.f, 10.f, 15.f));
}

TEST(MathVectorVec3OpsTest, ClampLengthAndMoveToward) {
    EXPECT_NEAR(clamp_length(Vec3f{0.f, 0.f, 10.f}, 4.f).length(), 4.f, 1e-5f);

    const Vec3f moved = move_toward(Vec3f{0.f, 0.f, 0.f}, Vec3f{0.f, 0.f, 10.f}, 4.f);
    EXPECT_NEAR(moved.z, 4.f, 1e-5f);
}

} // namespace
} // namespace beast::platform::bizutil::math
