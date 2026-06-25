#pragma once

#include "engine/demo_ai_engine.hpp"

#include "beast/platform/engine/ai/ai_receipt.hpp"

namespace beast::demo::ai::register4ai {

template <typename Engine>
    requires requires(Engine& engine, const HuntReceiptResult& result) {
        { engine.on_hunt_receipt(result) } -> std::same_as<void>;
    }
inline void register_hunt_receipt(Engine& engine, beast::platform::engine::ai::EngineAiHost& host) {
    beast::platform::engine::ai::register_json_receipt<HuntEvent, HuntReceiptResult, Engine>(
        host,
        engine,
        "demo_ai.hunt")
        .without_tools()
        .task(
            "你在地图上追杀一个会随机游走的小人，目标是尽快打死小人。"
            "每次投放一个 attack_square_size 大小的正方形攻击区域，输出左上角坐标。")
        .on_receipt(&Engine::on_hunt_receipt);
}

} // namespace beast::demo::ai::register4ai
