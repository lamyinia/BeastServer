#include "beast/platform/bizutil/math/random/sample.hpp"

#include <gtest/gtest.h>

#include <set>
#include <vector>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathRandomSampleTest, WeightedPickEmptyOrZeroWeightsReturnsNullopt) {
    SeededRng rng(1);
    const std::vector<WeightedEntry> empty{};
    EXPECT_FALSE(weighted_pick(rng, std::span<const WeightedEntry>{empty}).has_value());

    const std::vector<WeightedEntry> zero_weight{{1, 0}, {2, 0}};
    EXPECT_FALSE(weighted_pick(rng, zero_weight).has_value());

    const std::vector<int> zero_weights{0, 0, 0};
    EXPECT_FALSE(weighted_pick(rng, zero_weights).has_value());
}

TEST(MathRandomSampleTest, WeightedPickSingleEntryAlwaysReturnsZero) {
    SeededRng rng(99);
    const std::vector<WeightedEntry> single{{42, 10}};
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(weighted_pick(rng, single), 0U);
    }
}

TEST(MathRandomSampleTest, WeightedPickIsDeterministicForSameSeed) {
    const std::vector<WeightedEntry> pool{
        {101, 10},
        {102, 20},
        {103, 70},
    };

    SeededRng left(42);
    SeededRng right(42);
    for (int i = 0; i < 16; ++i) {
        EXPECT_EQ(weighted_pick(left, pool), weighted_pick(right, pool));
    }
}

TEST(MathRandomSampleTest, WeightedPickSkipsNonPositiveWeights) {
    const std::vector<int> weights{0, -1, 5, 0};
    SeededRng rng(7);
    for (int i = 0; i < 32; ++i) {
        EXPECT_EQ(weighted_pick(rng, weights), 2U);
    }
}

TEST(MathRandomSampleTest, WeightedSampleWithoutReplacementHasNoDuplicates) {
    const std::vector<WeightedEntry> pool{
        {1, 10},
        {2, 10},
        {3, 10},
        {4, 10},
    };

    SeededRng rng(123);
    const auto picked = weighted_sample_without_replacement(rng, pool, 3);
    ASSERT_EQ(picked.size(), 3U);
    EXPECT_EQ(std::set<std::size_t>(picked.begin(), picked.end()).size(), 3U);
}

TEST(MathRandomSampleTest, WeightedSampleWithoutReplacementCapsAtAvailableEntries) {
    const std::vector<int> weights{5, 5, 5};
    SeededRng rng(456);
    const auto picked = weighted_sample_without_replacement(rng, weights, 10);
    EXPECT_EQ(picked.size(), 3U);
}

TEST(MathRandomSampleTest, WeightedSampleWithoutReplacementIsDeterministic) {
    const std::vector<int> weights{1, 2, 3, 4, 5};

    SeededRng left(2024);
    SeededRng right(2024);
    EXPECT_EQ(
        weighted_sample_without_replacement(left, weights, 3),
        weighted_sample_without_replacement(right, weights, 3));
}

} // namespace
} // namespace beast::platform::bizutil::math
