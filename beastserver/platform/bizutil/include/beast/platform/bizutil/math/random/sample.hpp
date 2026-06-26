#pragma once

#include "beast/platform/bizutil/math/random/rng.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace beast::platform::bizutil::math {

struct WeightedEntry {
    int id{0};
    int weight{0};
};

// 按权重有放回抽取，返回 entries 下标；weight <= 0 的条目不参与。
[[nodiscard]] inline std::optional<std::size_t> weighted_pick(
    SeededRng& rng,
    const std::span<const WeightedEntry> entries) {
    int total = 0;
    for (const WeightedEntry& entry : entries) {
        if (entry.weight > 0) {
            total += entry.weight;
        }
    }
    if (total <= 0) {
        return std::nullopt;
    }

    const int roll = rng.uniform_int(1, total);
    int accumulated = 0;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].weight <= 0) {
            continue;
        }
        accumulated += entries[i].weight;
        if (roll <= accumulated) {
            return i;
        }
    }
    return entries.empty() ? std::nullopt : std::optional<std::size_t>{entries.size() - 1};
}

// 按权重有放回抽取，返回 weights 下标；weight <= 0 的条目不参与。
[[nodiscard]] inline std::optional<std::size_t> weighted_pick(
    SeededRng& rng,
    const std::span<const int> weights) {
    int total = 0;
    for (const int weight : weights) {
        if (weight > 0) {
            total += weight;
        }
    }
    if (total <= 0) {
        return std::nullopt;
    }

    const int roll = rng.uniform_int(1, total);
    int accumulated = 0;
    for (std::size_t i = 0; i < weights.size(); ++i) {
        if (weights[i] <= 0) {
            continue;
        }
        accumulated += weights[i];
        if (roll <= accumulated) {
            return i;
        }
    }
    return weights.empty() ? std::nullopt : std::optional<std::size_t>{weights.size() - 1};
}

// 按权重无放回抽取，最多 count 个不重复下标；不足时返回能抽到的全部。
[[nodiscard]] inline std::vector<std::size_t> weighted_sample_without_replacement(
    SeededRng& rng,
    std::span<const int> weights,
    const std::size_t count) {
    std::vector<int> remaining(weights.begin(), weights.end());
    std::vector<std::size_t> picked;
    picked.reserve(count);

    for (std::size_t n = 0; n < count; ++n) {
        const std::optional<std::size_t> index = weighted_pick(rng, remaining);
        if (!index.has_value()) {
            break;
        }
        picked.push_back(*index);
        remaining[*index] = 0;
    }
    return picked;
}

[[nodiscard]] inline std::vector<std::size_t> weighted_sample_without_replacement(
    SeededRng& rng,
    const std::span<const WeightedEntry> entries,
    const std::size_t count) {
    std::vector<int> weights;
    weights.reserve(entries.size());
    for (const WeightedEntry& entry : entries) {
        weights.push_back(entry.weight);
    }
    return weighted_sample_without_replacement(rng, weights, count);
}

} // namespace beast::platform::bizutil::math
