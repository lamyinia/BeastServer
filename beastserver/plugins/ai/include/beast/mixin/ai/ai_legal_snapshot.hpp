#pragma once

#include <nlohmann/json.hpp>
#include "beast/mixin/ai/_platform_compat.hpp"

#include <initializer_list>
#include <string>
#include <unordered_set>
#include <vector>
#include <vector>

namespace beast::mixin::ai {

// 请求发起瞬间的合法动作快照；完成时对照此集验权，不对照 LLM 自述。
class AiLegalSnapshot {
public:
    AiLegalSnapshot() = default;

    static AiLegalSnapshot from_actions(std::initializer_list<std::string> actions) {
        AiLegalSnapshot snapshot;
        snapshot.allowed_.insert(actions.begin(), actions.end());
        return snapshot;
    }

    static AiLegalSnapshot from_vector(const std::vector<std::string>& actions) {
        AiLegalSnapshot snapshot;
        snapshot.allowed_.insert(actions.begin(), actions.end());
        return snapshot;
    }

    [[nodiscard]] bool empty() const noexcept { return allowed_.empty(); }

    [[nodiscard]] bool allows(const std::string& action_id) const {
        return allowed_.contains(action_id);
    }

    [[nodiscard]] std::vector<std::string> actions() const {
        return {allowed_.begin(), allowed_.end()};
    }

    [[nodiscard]] std::string to_json() const {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& action : allowed_) {
            j.push_back(action);
        }
        return j.dump();
    }

private:
    std::unordered_set<std::string> allowed_;
};

} // namespace beast::mixin::ai
