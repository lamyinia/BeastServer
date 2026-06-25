#pragma once

#include "beast/platform/ai/model/chat.hpp"
#include "beast/platform/core/types.hpp"
#include "beast/platform/engine/ai/ai_legal_snapshot.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <utility>
#include <vector>

namespace beast::demo::ai2 {

struct MissionBrief {
    std::string codename;
    int threat_level{0};
    int squad_size{0};
    std::vector<std::string> objectives;
    std::vector<std::string> constraints;
};

struct IndexedOption {
    std::string option_id;
    std::string label;
    std::string summary;
};

inline std::vector<IndexedOption> index_options(const std::vector<std::pair<std::string, std::string>>& defs) {
    std::vector<IndexedOption> options;
    options.reserve(defs.size());
    for (std::size_t i = 0; i < defs.size(); ++i) {
        options.push_back(IndexedOption{
            .option_id = std::to_string(i),
            .label = defs[i].first,
            .summary = defs[i].second,
        });
    }
    return options;
}

inline nlohmann::json options_to_json(const std::vector<IndexedOption>& options) {
    nlohmann::json array = nlohmann::json::array();
    for (const auto& option : options) {
        array.push_back(nlohmann::json{
            {"option_id", option.option_id},
            {"label", option.label},
            {"summary", option.summary},
        });
    }
    return array;
}

// 一次 Decision：AI 从多种行为中选一种，输出带 action 字段的 JSON。
class MixedBehaviorDecision {
public:
    [[nodiscard]] static const char* task_prompt() noexcept {
        return "你是任务简报 bot。阅读 user JSON 场况，从合法 behaviors 中随机选一种行为，"
               "按该行为 required 字段输出 JSON（必须含 action 字段）。";
    }

    MixedBehaviorDecision(beast::platform::ActorId actor_id, MissionBrief brief)
        : actor_id_(std::move(actor_id))
        , brief_(std::move(brief))
        , routes_(index_options({
              {"stealth", "低噪音渗透，绕开主火力"},
              {"assault", "正面突破，快速压制"},
              {"negotiate", "接触内线，尝试非致命解"},
          }))
        , plans_(index_options({
              {"plan_alpha", "双前锋推进，侧翼牵制"},
              {"plan_beta", "佯攻正门，主力切后"},
              {"plan_gamma", "无人机侦察后定点清除"},
          }))
        , loadouts_(index_options({
              {"loadout_a", "均衡型：主武器 + 轻甲 + 双技能"},
              {"loadout_b", "重火力：重武 + 重甲 + 无人机"},
          }))
        , legal_(beast::platform::engine::ai::AiLegalSnapshot::from_actions({
              "ack",
              "pick_route",
              "squad_plan",
              "loadout",
          })) {}

    [[nodiscard]] beast::platform::ActorId actor_id() const noexcept { return actor_id_; }
    [[nodiscard]] const MissionBrief& brief() const noexcept { return brief_; }
    [[nodiscard]] beast::platform::engine::ai::AiLegalSnapshot legal_snapshot() const { return legal_; }
    [[nodiscard]] const std::vector<IndexedOption>& routes() const noexcept { return routes_; }
    [[nodiscard]] const std::vector<IndexedOption>& plans() const noexcept { return plans_; }
    [[nodiscard]] const std::vector<IndexedOption>& loadouts() const noexcept { return loadouts_; }

    [[nodiscard]] const IndexedOption* find_route(const std::string& route_id) const {
        for (const auto& route : routes_) {
            if (route.option_id == route_id) {
                return &route;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::vector<beast::platform::ai::Message> to_messages() const {
        const nlohmann::json observation = {
            {"brief",
             {
                 {"codename", brief_.codename},
                 {"threat_level", brief_.threat_level},
                 {"squad_size", brief_.squad_size},
                 {"objectives", brief_.objectives},
                 {"constraints", brief_.constraints},
             }},
            {"behaviors",
             nlohmann::json::array({
                 nlohmann::json{
                     {"action", "ack"},
                     {"summary", "确认收到简报"},
                 },
                 nlohmann::json{
                     {"action", "pick_route"},
                     {"options", options_to_json(routes_)},
                 },
                 nlohmann::json{
                     {"action", "squad_plan"},
                     {"options", options_to_json(plans_)},
                 },
                 nlohmann::json{
                     {"action", "loadout"},
                     {"options", options_to_json(loadouts_)},
                 },
             })},
            {"instruction",
             "从 behaviors 中随机选择一种 action，输出带 action 字段的 JSON；"
             "不要 markdown，不要额外字段。"},
        };

        return {beast::platform::ai::Message::user(observation.dump())};
    }

private:
    beast::platform::ActorId actor_id_;
    MissionBrief brief_;
    std::vector<IndexedOption> routes_;
    std::vector<IndexedOption> plans_;
    std::vector<IndexedOption> loadouts_;
    beast::platform::engine::ai::AiLegalSnapshot legal_;
};

} // namespace beast::demo::ai2
