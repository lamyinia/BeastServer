#include "algo/tenpai.hpp"

#include <array>
#include <optional>
#include <string>
#include <utility>

namespace beast::demo::stress_tick {
namespace {

constexpr int kSuits = 4;
constexpr int kMaxRank = 9;

using TileCounts = std::array<std::array<int, kMaxRank + 1>, kSuits>;

[[nodiscard]] std::optional<std::pair<int, int>> parse_tile(const std::string& tile) {
    if (tile.empty()) return std::nullopt;

    if (tile == "east")  return std::pair<int, int>{3, 1};
    if (tile == "south") return std::pair<int, int>{3, 2};
    if (tile == "west")  return std::pair<int, int>{3, 3};
    if (tile == "north") return std::pair<int, int>{3, 4};

    if (tile.size() < 2) return std::nullopt;
    const int rank = tile.front() - '0';
    if (rank < 1 || rank > 9) return std::nullopt;

    const char suit_ch = tile.back();
    switch (suit_ch) {
        case 'm': return std::pair<int, int>{0, rank};
        case 'p': return std::pair<int, int>{1, rank};
        case 's': return std::pair<int, int>{2, rank};
        default:  return std::nullopt;
    }
}

[[nodiscard]] bool to_counts(const std::vector<std::string>& tiles, TileCounts& out) {
    for (const auto& tile : tiles) {
        const auto idx = parse_tile(tile);
        if (!idx) return false;
        const auto [suit, rank] = *idx;
        if (out[suit][rank] >= 4) return false;
        ++out[suit][rank];
    }
    return true;
}

[[nodiscard]] bool can_form_melds(TileCounts& counts, int n_melds) {
    if (n_melds == 0) {
        for (int s = 0; s < kSuits; ++s) {
            for (int r = 1; r <= kMaxRank; ++r) {
                if (counts[s][r] != 0) return false;
            }
        }
        return true;
    }

    for (int s = 0; s < kSuits; ++s) {
        for (int r = 1; r <= kMaxRank; ++r) {
            if (counts[s][r] == 0) continue;

            if (counts[s][r] >= 3) {
                counts[s][r] -= 3;
                if (can_form_melds(counts, n_melds - 1)) {
                    counts[s][r] += 3;
                    return true;
                }
                counts[s][r] += 3;
            }

            if (s != 3 && r <= 7 &&
                counts[s][r + 1] >= 1 && counts[s][r + 2] >= 1) {
                --counts[s][r];
                --counts[s][r + 1];
                --counts[s][r + 2];
                if (can_form_melds(counts, n_melds - 1)) {
                    ++counts[s][r];
                    ++counts[s][r + 1];
                    ++counts[s][r + 2];
                    return true;
                }
                ++counts[s][r];
                ++counts[s][r + 1];
                ++counts[s][r + 2];
            }

            return false;
        }
    }
    return true;
}

[[nodiscard]] bool is_hu_14(const TileCounts& counts_in) {
    TileCounts counts = counts_in;

    for (int s = 0; s < kSuits; ++s) {
        for (int r = 1; r <= kMaxRank; ++r) {
            if (counts[s][r] < 2) continue;

            counts[s][r] -= 2;
            if (can_form_melds(counts, 4)) {
                counts[s][r] += 2;
                return true;
            }
            counts[s][r] += 2;
        }
    }
    return false;
}

} // namespace

bool is_tenpai(const std::vector<std::string>& tiles_13) {
    if (tiles_13.size() != 13) return false;

    TileCounts counts{};
    if (!to_counts(tiles_13, counts)) return false;

    for (int s = 0; s < kSuits; ++s) {
        const int max_rank = (s == 3) ? 4 : 9;
        for (int r = 1; r <= max_rank; ++r) {
            if (counts[s][r] >= 4) continue;

            TileCounts try_counts = counts;
            ++try_counts[s][r];
            if (is_hu_14(try_counts)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace beast::demo::stress_tick
