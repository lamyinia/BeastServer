#include "beast/platform/bizutil/math/geometry/cone.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathGeometryConeTest, ContainsPointInSector) {
    const Conef cone{
        {0.f, 0.f},
        {1.f, 0.f},
        10.f,
        static_cast<float>(M_PI / 2.f),
    };

    EXPECT_TRUE(contains(cone, Vec2f{5.f, 0.f}));
    EXPECT_TRUE(contains(cone, Vec2f{4.f, 4.f}));
    EXPECT_FALSE(contains(cone, Vec2f{-1.f, 0.f}));
    EXPECT_FALSE(contains(cone, Vec2f{20.f, 0.f}));
}

} // namespace
} // namespace beast::platform::bizutil::math
