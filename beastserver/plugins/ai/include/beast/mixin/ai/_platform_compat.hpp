// 命名空间兼容层（内部头文件，仅 plugins/ai 内部使用）
//
// plugins/ai 从 beast::platform::engine::ai 迁移到 beast::mixin::ai 后，原相对
// 引用（context:: / instance:: / ActorId 等）不再能通过命名空间嵌套自动解析。
// 此头文件在 beast::mixin::ai 内建立别名与 using 声明，使旧代码无需逐处全限定。
//
// 注意：文件名以 _ 开头表示内部头文件，不应被 plugins/ai 之外的代码直接 include。

#pragma once

#include "beast/platform/core/types.hpp"

// 前向声明 engine 子命名空间中的类型，避免引入完整定义造成重 include 开销。
namespace beast::platform::engine::context {
class EngineContext;
}
namespace beast::platform::engine::instance {
struct InstanceEvent;
}

namespace beast::mixin::ai {
// platform 基础类型（types.hpp 在 beast::platform 下 using 了 core::* 类型）
using beast::platform::ActorId;
using beast::platform::ActorKind;
using beast::platform::EntityId;
using beast::platform::InstanceId;
using beast::platform::PlayerId;
using beast::platform::RouteId;
using beast::platform::SessionId;
using beast::platform::SimulationMode;
using beast::platform::Tick;
using beast::platform::TimestampMs;

// engine 子命名空间别名，使 context::Foo / instance::Foo 相对引用继续可用
namespace context = beast::platform::engine::context;
namespace instance = beast::platform::engine::instance;
} // namespace beast::mixin::ai
