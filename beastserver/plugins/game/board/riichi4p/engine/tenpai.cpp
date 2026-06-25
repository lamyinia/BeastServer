#include "tenpai.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace beast::board::riichi4p {
namespace {

struct TileKey {
    int suit{-1};  // 0=m 1=p 2=s 3=honor
    int rank{0};   // 1-9 for suits; honor 1=east..7=red
};

[[nodiscard]] std::optional<TileKey> parse_tile(const std::string& tile) {
    if (tile.size() < 2) {
        if (tile == "east") {
            return TileKey{.suit = 3, .rank = 1};
        }
        if (tile == "south") {
            return TileKey{.suit = 3, .rank = 2};
        }
        if (tile == "west") {
            return TileKey{.suit = 3, .rank = 3};
        }
        if (tile == "north") {
            return TileKey{.suit = 3, .rank = 4};
        }
        return std::nullopt;
    }

    const char suit_ch = tile.back();
    const int rank = tile.front() - '0';
    if (rank < 1 || rank > 9) {
        return std::nullopt;
    }

    switch (suit_ch) {
    case 'm':
        return TileKey{.suit = 0, .rank = rank};
    case 'p':
        return TileKey{.suit = 1, .rank = rank};
    case 's':
        return TileKey{.suit = 2, .rank = rank};
    default:
        return std::nullopt;
    }
}

using TileCounts = std::map<std::string, int>;

[[nodiscard]] TileCounts to_counts(const std::vector<std::string>& tiles) {
    TileCounts counts;
    for (const auto& tile : tiles) {
        ++counts[tile];
    }
    return counts;
}

[[nodiscard]] bool can_form_groups(TileCounts counts, int groups_left) {
    if (groups_left == 0) {
        return counts.empty();
    }

    if (counts.empty()) {
        return false;
    }

    const auto it = counts.begin();
    const std::string tile = it->first;
    const int count = it->second;
    const auto parsed = parse_tile(tile);
    if (!parsed.has_value()) {
        return false;
    }

    if (count >= 3) {
        auto next = counts;
        next[tile] -= 3;
        if (next[tile] == 0) {
            next.erase(tile);
        }
        if (can_form_groups(std::move(next), groups_left - 1)) {
            return true;
        }
    }

    if (parsed->suit >= 0 && parsed->suit <= 2 && parsed->rank <= 7) {
        const char suit = tile.back();
        const std::string t2 = std::string{static_cast<char>('0' + parsed->rank + 1), suit};
        const std::string t3 = std::string{static_cast<char>('0' + parsed->rank + 2), suit};
        const auto it2 = counts.find(t2);
        const auto it3 = counts.find(t3);
        if (it2 != counts.end() && it2->second > 0 && it3 != counts.end() && it3->second > 0) {
            auto next = counts;
            --next[tile];
            if (next[tile] == 0) {
                next.erase(tile);
            }
            --next[t2];
            if (next[t2] == 0) {
                next.erase(t2);
            }
            --next[t3];
            if (next[t3] == 0) {
                next.erase(t3);
            }
            if (can_form_groups(std::move(next), groups_left - 1)) {
                return true;
            }
        }
    }

    return false;
}

[[nodiscard]] bool is_winning_hand(TileCounts counts) {
    if (counts.size() == 0) {
        return true;
    }

    for (auto& [tile, count] : counts) {
        if (count < 2) {
            continue;
        }
        auto rest = counts;
        rest[tile] -= 2;
        if (rest[tile] == 0) {
            rest.erase(tile);
        }
        if (can_form_groups(std::move(rest), 4)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::vector<std::string> all_tile_universe() {
    std::vector<std::string> tiles;
    for (const char suit : {'m', 'p', 's'}) {
        for (int rank = 1; rank <= 9; ++rank) {
            tiles.push_back(std::string{static_cast<char>('0' + rank), suit});
        }
    }
    tiles.push_back("east");
    tiles.push_back("south");
    tiles.push_back("west");
    tiles.push_back("north");
    return tiles;
}

} // namespace

bool is_tenpai(const std::vector<std::string>& tiles_13) {
    if (tiles_13.size() != 13) {
        return false;
    }

    const TileCounts base = to_counts(tiles_13);
    for (const auto& candidate : all_tile_universe()) {
        if (base.contains(candidate) && base.at(candidate) >= 4) {
            continue;
        }
        auto extended = base;
        ++extended[candidate];
        if (is_winning_hand(std::move(extended))) {
            return true;
        }
    }
    return false;
}

bool is_tenpai_after_discard(
    const std::vector<std::string>& tiles_14,
    const std::string& discard) {
    if (tiles_14.size() != 14) {
        return false;
    }

    auto hand = tiles_14;
    const auto it = std::find(hand.begin(), hand.end(), discard);
    if (it == hand.end()) {
        return false;
    }
    hand.erase(it);
    return is_tenpai(hand);
}

} // namespace beast::board::riichi4p
