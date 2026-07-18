#pragma once

#include "beast/platform/ai/model/chat.hpp"
#include "beast/platform/core/types.hpp"
#include "beast/mixin/ai/ai_legal_snapshot.hpp"
#include "engine/table_state.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace beast::board::riichi4p {

struct IndexedTurnAction {
    std::string action_id;
    TurnAction action;
};

inline nlohmann::json indexed_turn_action_to_json(const IndexedTurnAction& entry) {
    return nlohmann::json{
        {"action_id", entry.action_id},
        {"kind", turn_action_kind_name(entry.action.kind)},
        {"tile", entry.action.tile},
    };
}

inline std::vector<IndexedTurnAction> index_turn_actions(const std::vector<TurnAction>& actions) {
    std::vector<IndexedTurnAction> indexed;
    indexed.reserve(actions.size());
    for (std::size_t i = 0; i < actions.size(); ++i) {
        indexed.push_back(IndexedTurnAction{
            .action_id = std::to_string(i),
            .action = actions[i],
        });
    }
    return indexed;
}

// 待出牌 Decision：legal_actions 快照 + opaque action_id 选型。
class SuggestTurnActionDecision {
public:
    [[nodiscard]] static const char* task_prompt() noexcept {
        return "你是日本麻将（立直麻将）bot。阅读 user JSON 场况，从 legal_actions 中选择一项。";
    }

    SuggestTurnActionDecision(
        beast::platform::ActorId actor_id,
        int seat,
        RiichiTableView view)
        : actor_id_(std::move(actor_id))
        , seat_(seat)
        , view_(std::move(view))
        , legal_actions_(index_turn_actions(view_.legal_actions))
        , legal_(beast::mixin::ai::AiLegalSnapshot::from_vector(collect_action_kinds())) {}

    [[nodiscard]] beast::platform::ActorId actor_id() const noexcept { return actor_id_; }
    [[nodiscard]] int seat() const noexcept { return seat_; }
    [[nodiscard]] const RiichiTableView& view() const noexcept { return view_; }
    [[nodiscard]] const std::vector<IndexedTurnAction>& legal_actions() const noexcept {
        return legal_actions_;
    }
    [[nodiscard]] beast::mixin::ai::AiLegalSnapshot legal_snapshot() const {
        return legal_;
    }

    [[nodiscard]] const TurnAction* find_action(const std::string& action_id) const {
        for (const auto& entry : legal_actions_) {
            if (entry.action_id == action_id) {
                return &entry.action;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::vector<beast::platform::ai::Message> to_messages() const {
        nlohmann::json rivers = nlohmann::json::array();
        for (const auto& river : view_.rivers) {
            rivers.push_back(river);
        }

        nlohmann::json melds = nlohmann::json::array();
        for (const auto& meld : view_.melds) {
            melds.push_back(meld);
        }

        nlohmann::json legal_actions = nlohmann::json::array();
        for (const auto& entry : legal_actions_) {
            legal_actions.push_back(indexed_turn_action_to_json(entry));
        }

        const nlohmann::json observation = {
            {"tile_notation", "m=万(w), p=筒(p), s=索(s)"},
            {"round_wind", view_.round_wind},
            {"dealer_seat", view_.dealer_seat},
            {"self_seat", view_.self_seat},
            {"self_wind", view_.self_wind},
            {"hand", view_.hand},
            {"rivers", rivers},
            {"melds", melds},
            {"dora_indicators", view_.dora_indicators},
            {"can_riichi", view_.can_riichi},
            {"riichi_declared", view_.riichi_declared},
            {"legal_actions", legal_actions},
        };

        return {beast::platform::ai::Message::user(observation.dump())};
    }

private:
    [[nodiscard]] std::vector<std::string> collect_action_kinds() const {
        bool has_discard = false;
        bool has_riichi = false;
        for (const auto& entry : legal_actions_) {
            if (entry.action.kind == TurnActionKind::Discard) {
                has_discard = true;
            }
            if (entry.action.kind == TurnActionKind::Riichi) {
                has_riichi = true;
            }
        }

        std::vector<std::string> kinds;
        if (has_discard) {
            kinds.push_back(turn_action_kind_name(TurnActionKind::Discard));
        }
        if (has_riichi) {
            kinds.push_back(turn_action_kind_name(TurnActionKind::Riichi));
        }
        return kinds;
    }

    beast::platform::ActorId actor_id_;
    int seat_;
    RiichiTableView view_;
    std::vector<IndexedTurnAction> legal_actions_;
    beast::mixin::ai::AiLegalSnapshot legal_;
};

} // namespace beast::board::riichi4p
