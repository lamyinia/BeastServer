#include "algo/tenpai.hpp"

#include <array>
#include <optional>
#include <string>
#include <utility>

namespace beast::demo::stress_event {
namespace {

// 牌索引：(suit, rank)
//   suit: 0=m(数子) 1=p(筒) 2=s(索) 3=honor(字)
//   rank: 1-9 for suits; 1-4 for honor (east/south/west/north)
// 数量上限：每种牌 4 张
constexpr int kSuits = 4;
constexpr int kMaxRank = 9;  // honor 只用 1-4，但统一开到 9 简化下标

// 用 [suit][rank] 二维数组表示手牌计数（rank 索引 1..9，0 弃用）
using TileCounts = std::array<std::array<int, kMaxRank + 1>, kSuits>;

[[nodiscard]] std::optional<std::pair<int, int>> parse_tile(const std::string& tile) {
    if (tile.empty()) return std::nullopt;

    // 字牌（无 suit 字符）
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
        if (out[suit][rank] >= 4) return false;  // 单种牌上限 4
        ++out[suit][rank];
    }
    return true;
}

// 递归判断 counts 剩余的牌能否拆成 n_melds 个面子（顺子/刻子）
// 调用前置条件：counts 总数 == n_melds * 3
[[nodiscard]] bool can_form_melds(TileCounts& counts, int n_melds) {
    if (n_melds == 0) {
        // 检查全部清空
        for (int s = 0; s < kSuits; ++s) {
            for (int r = 1; r <= kMaxRank; ++r) {
                if (counts[s][r] != 0) return false;
            }
        }
        return true;
    }

    // 找第一个非零的 (suit, rank)
    for (int s = 0; s < kSuits; ++s) {
        for (int r = 1; r <= kMaxRank; ++r) {
            if (counts[s][r] == 0) continue;

            // 尝试刻子（triplet）
            if (counts[s][r] >= 3) {
                counts[s][r] -= 3;
                if (can_form_melds(counts, n_melds - 1)) {
                    counts[s][r] += 3;
                    return true;
                }
                counts[s][r] += 3;
            }

            // 尝试顺子（sequence）—— 字牌不能组顺子
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

            // 当前牌无法组面子，整体失败
            return false;
        }
    }
    return true;  // 全部为 0
}

// 14 张牌是否胡牌（4 面子 + 1 雀头）
[[nodiscard]] bool is_hu_14(const TileCounts& counts_in) {
    TileCounts counts = counts_in;

    // 尝试每个 (suit, rank) 作雀头
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

    // 遍历所有可能的"听牌"X：tiles_13 + X 凑成 14 张胡牌
    for (int s = 0; s < kSuits; ++s) {
        const int max_rank = (s == 3) ? 4 : 9;
        for (int r = 1; r <= max_rank; ++r) {
            if (counts[s][r] >= 4) continue;  // 已有 4 张，不能再加

            TileCounts try_counts = counts;
            ++try_counts[s][r];
            if (is_hu_14(try_counts)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace beast::demo::stress_event
