#include "beast/platform/bizutil/math/vector/vec3.hpp"

#include <gtest/gtest.h>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathVectorVec3Test, AdditionSubtractionAndLength) {
    const Vec3f a{1.f, 2.f, 2.f};
    const Vec3f b{1.f, 0.f, 0.f};
    EXPECT_EQ(a + b, Vec3f(2.f, 2.f, 2.f));
    EXPECT_EQ(a - b, Vec3f(0.f, 2.f, 2.f));
    EXPECT_FLOAT_EQ(a.length_squared(), 9.f);
    EXPECT_FLOAT_EQ(a.length(), 3.f);
}

TEST(MathVectorVec3Test, IntegerVectorEquality) {
    EXPECT_EQ(Vec3i(1, 2, 3) + Vec3i(1, 1, 1), Vec3i(2, 3, 4));
    EXPECT_EQ(to_vec3i(Vec3f{1.9f, 2.1f, 3.5f}), Vec3i(1, 2, 3));
}

} // namespace
} // namespace beast::platform::bizutil::math
