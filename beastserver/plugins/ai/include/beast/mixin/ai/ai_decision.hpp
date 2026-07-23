#pragma once

#include "beast/mixin/ai/model/chat.hpp"
#include "beast/mixin/ai/_platform_compat.hpp"
#include "beast/platform/core/types.hpp"
#include "beast/mixin/ai/ai_legal_snapshot.hpp"

#include <concepts>
#include <string>
#include <vector>

namespace beast::mixin::ai {

// 玩法 Decision：自带观测、合法集与 actor_id；平台只负责管道与验权。
template <typename T>
concept AiDecision = requires(const T& decision) {
    { decision.actor_id() } -> std::convertible_to<ActorId>;
    { decision.to_messages() } -> std::same_as<std::vector<platform::ai::Message>>;
    { decision.legal_snapshot() } -> std::same_as<AiLegalSnapshot>;
};

} // namespace beast::mixin::ai
