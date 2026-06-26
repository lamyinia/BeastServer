#include "beast/platform/bizutil/math/random/rng.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathRandomSeededRngTest, SameSeedProducesSameSequence) {
    SeededRng a(42);
    SeededRng b(42);

    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(a.uniform_int(0, 100), b.uniform_int(0, 100));
    }
}

TEST(MathRandomSeededRngTest, UniformIntRespectsRange) {
    SeededRng rng(7);
    for (int i = 0; i < 64; ++i) {
        const int value = rng.uniform_int(3, 9);
        EXPECT_GE(value, 3);
        EXPECT_LE(value, 9);
    }
}

TEST(MathRandomSeededRngTest, ShuffleIsDeterministic) {
    std::vector<int> left{1, 2, 3, 4, 5};
    std::vector<int> right = left;

    SeededRng(99).shuffle(left.begin(), left.end());
    SeededRng(99).shuffle(right.begin(), right.end());
    EXPECT_EQ(left, right);
}

} // namespace
} // namespace beast::platform::bizutil::math
