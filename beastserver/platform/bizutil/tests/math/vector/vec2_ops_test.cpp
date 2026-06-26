#include "beast/platform/bizutil/math/vector/vec2_ops.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathVectorVec2OpsTest, DotCrossDistanceAndNormalize) {
    const Vec2f a{1.f, 0.f};
    const Vec2f b{0.f, 1.f};
    EXPECT_FLOAT_EQ(dot(a, b), 0.f);
    EXPECT_FLOAT_EQ(cross(a, b), 1.f);
    EXPECT_FLOAT_EQ(distance(Vec2f{0.f, 0.f}, Vec2f{3.f, 4.f}), 5.f);

    const Vec2f n = normalize(Vec2f{3.f, 4.f});
    EXPECT_NEAR(n.length(), 1.f, 1e-5f);
}

} // namespace
} // namespace beast::platform::bizutil::math
