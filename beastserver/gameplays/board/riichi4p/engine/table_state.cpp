#include "table_state.hpp"
#include "tenpai.hpp"

#include <algorithm>
#include <unordered_set>

namespace beast::board::riichi4p {
namespace {

constexpr const char* kWindNames[] = {"east", "south", "west", "north"};

// 规范牌序：万(0-8) < 筒(9-17) < 索(18-26) < 字牌(27+)。
// 兼容字牌的两种写法（"east"/"south"/... 与 "1z".."7z"），未知牌排在最后。
[[nodiscard]] int tile_order_key(const std::string& tile) {
    constexpr int kHonorBase = 27;
    constexpr int kUnknown = 1000;

    if (tile == "east") {
        return kHonorBase + 0;
    }
    if (tile == "south") {
        return kHonorBase + 1;
    }
    if (tile == "west") {
        return kHonorBase + 2;
    }
    if (tile == "north") {
        return kHonorBase + 3;
    }

    if (tile.size() != 2) {
        return kUnknown;
    }
    const char rank_ch = tile.front();
    const char suit_ch = tile.back();
    if (rank_ch < '1' || rank_ch > '9') {
        return kUnknown;
    }
    const int rank = rank_ch - '1';
    switch (suit_ch) {
    case 'm':
        return rank;
    case 'p':
        return 9 + rank;
    case 's':
        return 18 + rank;
    case 'z':
        return kHonorBase + rank;
    default:
        return kUnknown;
    }
}

} // namespace

void RiichiTableState::init_sample_deal() {
    round_wind_ = 0;
    dealer_seat_ = 0;
    self_seat_ = 0;
    can_riichi_ = true;
    riichi_declared_ = false;
    dora_indicators_ = {"5m"};

    hands_[0] = {
        "2m", "3m", "4m", "6m", "7m", "9m", "9m", "9m", "3p", "6s", "7s", "8s", "8s", "8s",
    };
    hands_[1] = {
        "2m", "3m", "4m", "6p", "7p", "8p", "1s", "2s", "3s", "4s", "5s", "6s", "7s",
    };
    hands_[2] = {
        "5m", "6m", "7m", "8m", "9m", "2p", "3p", "4p", "8s", "9s", "1z", "2z", "3z",
    };
    hands_[3] = {
        "1m", "9m", "1p", "9p", "1s", "9s", "east", "east", "south", "south", "west", "west", "north",
    };

    for (auto& river : rivers_) {
        river.clear();
    }
    for (auto& meld : melds_) {
        meld.clear();
    }

    rivers_[1] = {"3p", "6m"};
    rivers_[2] = {"7p"};
    rivers_[3] = {"2s", "4s", "8p"};
    melds_[2] = {"pon:5p"};
}

std::string RiichiTableState::seat_wind_name(const int dealer_seat, const int seat) {
    const int offset = (seat - dealer_seat + kSeatCount) % kSeatCount;
    return kWindNames[offset];
}

std::vector<std::string> RiichiTableState::unique_tiles_in_hand(const int seat) const {
    std::unordered_set<std::string> unique;
    for (const auto& tile : hands_.at(static_cast<std::size_t>(seat))) {
        unique.insert(tile);
    }
    std::vector<std::string> tiles(unique.begin(), unique.end());
    // 规范排序：保证同一手牌每次生成的 legal_actions / action_id 完全确定。
    std::sort(tiles.begin(), tiles.end(), [](const std::string& lhs, const std::string& rhs) {
        const int lk = tile_order_key(lhs);
        const int rk = tile_order_key(rhs);
        if (lk != rk) {
            return lk < rk;
        }
        return lhs < rhs;
    });
    return tiles;
}

RiichiTableView RiichiTableState::make_view(const int seat) const {
    RiichiTableView view;
    view.round_wind = kWindNames[round_wind_ % kSeatCount];
    view.dealer_seat = dealer_seat_;
    view.self_seat = seat;
    view.self_wind = seat_wind_name(dealer_seat_, seat);
    view.hand = hands_.at(static_cast<std::size_t>(seat));
    view.rivers = rivers_;
    view.melds = melds_;
    view.dora_indicators = dora_indicators_;
    view.legal_actions = legal_turn_actions(seat);
    const bool seat_closed = melds_.at(static_cast<std::size_t>(seat)).empty();
    view.can_riichi = can_riichi_ && !riichi_declared_ && seat_closed &&
                        std::any_of(
                            view.legal_actions.begin(),
                            view.legal_actions.end(),
                            [](const TurnAction& action) {
                                return action.kind == TurnActionKind::Riichi;
                            });
    view.riichi_declared = riichi_declared_;
    return view;
}

std::vector<TurnAction> RiichiTableState::legal_turn_actions(const int seat) const {
    std::vector<TurnAction> actions;
    const auto& hand = hands_.at(static_cast<std::size_t>(seat));
    const auto tiles = unique_tiles_in_hand(seat);
    const bool seat_closed = melds_.at(static_cast<std::size_t>(seat)).empty();
    const bool riichi_allowed = can_riichi_ && !riichi_declared_ && seat_closed;

    for (const auto& tile : tiles) {
        actions.push_back(TurnAction{
            .kind = TurnActionKind::Discard,
            .tile = tile,
        });
        if (riichi_allowed && is_tenpai_after_discard(hand, tile)) {
            actions.push_back(TurnAction{
                .kind = TurnActionKind::Riichi,
                .tile = tile,
            });
        }
    }
    return actions;
}

bool RiichiTableState::apply_turn_action(const int seat, const TurnAction& action) {
    auto& hand = hands_.at(static_cast<std::size_t>(seat));
    const auto it = std::find(hand.begin(), hand.end(), action.tile);
    if (it == hand.end()) {
        return false;
    }
    hand.erase(it);
    rivers_.at(static_cast<std::size_t>(seat)).push_back(action.tile);
    if (action.kind == TurnActionKind::Riichi) {
        riichi_declared_ = true;
    }
    return true;
}

} // namespace beast::board::riichi4p
