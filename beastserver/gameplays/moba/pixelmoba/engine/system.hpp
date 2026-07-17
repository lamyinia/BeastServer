#pragma once

#include "beast/platform/core/types.hpp"

namespace beast::platform::engine::context {
class EngineContext;
}

namespace beast::moba::pixel {

struct WorldState;

// 子系统接口。引擎持有所有 System 成员,on_tick 顺序驱动。
// ctx 与 world 在 on_start 注入(指针存储),tick 直接用。
//
// 快照不再由 System 贡献:engine 直接读 WorldState 构建分层 sync
// (TransformSync/AttrSync/...),System 只负责修改 WorldState + 标 dirty。
//
// consume 不在此声明:各子类按需提供 typed overload,如:
//   void consume(const PlayerId&, const MoveCmd&);
// 引擎 dispatch_input 用 std::visit 按 variant 类型调用对应 System 的 typed consume。
class System {
public:
    virtual ~System() = default;

    // 引擎 on_start 时逐个调用:注入 ctx + 共享世界状态,可加载配表/初始化。
    virtual void on_start(
        beast::platform::engine::context::EngineContext& ctx,
        WorldState& world) {}

    // 每帧模拟。tick=当前 tick 序号, dt_ms=帧间隔(60Hz≈16ms)。
    virtual void tick(beast::platform::Tick tick, beast::platform::TimestampMs dt_ms) {}
};

} // namespace beast::moba::pixel
