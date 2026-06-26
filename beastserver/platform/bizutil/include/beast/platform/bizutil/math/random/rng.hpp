#pragma once

#include <algorithm>
#include <cstdint>
#include <random>

namespace beast::platform::bizutil::math {

// 可播种 RNG：便于单测复现与局内确定性随机（由玩法传入 seed）。
class SeededRng {
public:
    explicit SeededRng(const std::uint64_t seed)
        : seed_(seed)
        , gen_(static_cast<std::mt19937::result_type>(seed)) {}

    [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }

    [[nodiscard]] int uniform_int(const int lo, const int hi) {
        std::uniform_int_distribution<int> dist(std::min(lo, hi), std::max(lo, hi));
        return dist(gen_);
    }

    [[nodiscard]] float uniform_float(const float lo, const float hi) {
        std::uniform_real_distribution<float> dist(std::min(lo, hi), std::max(lo, hi));
        return dist(gen_);
    }

    [[nodiscard]] bool coin_flip() { return uniform_int(0, 1) == 1; }

    template<typename EngineT>
    void shuffle(EngineT first, EngineT last) {
        std::shuffle(first, last, gen_);
    }

private:
    std::uint64_t seed_{0};
    std::mt19937 gen_;
};

} // namespace beast::platform::bizutil::math
