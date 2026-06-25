#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace beast::board::riichi4p {

inline constexpr int kSeatCount = 4;

enum class TurnActionKind { Discard, Riichi };

[[nodiscard]] inline std::string turn_action_kind_name(const TurnActionKind kind) {
    switch (kind) {
    case TurnActionKind::Discard:
        return "discard";
    case TurnActionKind::Riichi:
        return "riichi";
    }
    return "unknown";
}

struct TurnAction {
    TurnActionKind kind{};
    std::string tile;
};

struct RiichiTableView {
    std::string round_wind;
    int dealer_seat{0};
    int self_seat{0};
    std::string self_wind;
    std::vector<std::string> hand;
    std::array<std::vector<std::string>, kSeatCount> rivers{};
    std::array<std::vector<std::string>, kSeatCount> melds{};
    std::vector<std::string> dora_indicators;
    bool can_riichi{false};
    bool riichi_declared{false};
    std::vector<TurnAction> legal_actions;
};

class RiichiTableState {
public:
    void init_sample_deal();

    [[nodiscard]] int self_seat() const noexcept { return self_seat_; }
    [[nodiscard]] RiichiTableView make_view(int seat) const;
    [[nodiscard]] std::vector<TurnAction> legal_turn_actions(int seat) const;
    [[nodiscard]] bool apply_turn_action(int seat, const TurnAction& action);

private:
    static std::string seat_wind_name(int dealer_seat, int seat);
    [[nodiscard]] std::vector<std::string> unique_tiles_in_hand(int seat) const;

    int round_wind_{0};
    int dealer_seat_{0};
    int self_seat_{0};
    bool can_riichi_{false};
    bool riichi_declared_{false};
    std::array<std::vector<std::string>, kSeatCount> hands_{};
    std::array<std::vector<std::string>, kSeatCount> rivers_{};
    std::array<std::vector<std::string>, kSeatCount> melds_{};
    std::vector<std::string> dora_indicators_{"5m"};
};

} // namespace beast::board::riichi4p
