#pragma once

#include "beast/platform/core/types.hpp"
#include "beast/platform/engine/instance/i_engine.hpp"
#include "beast/platform/engine/instance/instance_event.hpp"

#include "sdk_event.pb.h"

#include <memory>

namespace beast::demo::event {

// SdkEventEngine: SDK 联调专用玩法引擎 (v1 = Echo 4 件套)
//
// 设计目标：覆盖客户端 SDK 协议栈测试的最小集，EventDriven 模式（无 tick）
//   1. sdk.echo        string 回显                - 验证基本通信
//   2. sdk.echo.seq    client_seq 透传            - 验证 req/resp 配对
//   3. sdk.echo.bytes  bytes payload 透传         - 验证二进制不破坏（含 0x00/0xFF）
//   4. sdk.echo.big    服务端构造 size bytes 回包 - 验证大消息 frame 边界
//
// 与 demo_event 的区别：demo_event 只测 string echo；sdk_event 增加 bytes/big/seq
// 路由，专门覆盖 SDK 协议栈边界场景。
class SdkEventEngine final : public beast::platform::engine::instance::IEngine {
public:
    void on_start(beast::platform::engine::context::EngineContext& ctx) override;
    void on_event(const beast::platform::engine::instance::InstanceEvent& event) override;

private:
    // 基本回显：text 原样返回
    void on_echo(
        const beast::platform::PlayerId& player_id,
        const beast::demo::EchoRequest& req);

    // client_seq 透传：用 PROTO_THIS 拿到完整 InstanceEvent，回包带 client_seq
    void on_seq_echo(
        const beast::platform::engine::instance::InstanceEvent& event,
        const beast::demo::SeqEchoRequest& req);

    // bytes 透传：原样回 bytes，验证二进制不被破坏
    void on_bytes_echo(
        const beast::platform::PlayerId& player_id,
        const beast::demo::BytesEchoRequest& req);

    // 大消息：服务端按 size 构造 payload（byte[i] = i & 0xFF 模式填充，客户端可校验完整性）
    void on_big_echo(
        const beast::platform::PlayerId& player_id,
        const beast::demo::BigEchoRequest& req);

    // Transport 调度测试：客户端指定 preference，服务端用 OutboundHub 按 preference
    // 跨 channel 调度回包。客户端观察实际从哪个 channel 收到 resp，验证调度生效。
    void on_transport_info(
        const beast::platform::engine::instance::InstanceEvent& event,
        const beast::demo::TransportInfoRequest& req);

    beast::platform::engine::context::EngineContext* ctx_{nullptr};
};

std::unique_ptr<SdkEventEngine> make_sdk_event_engine();

} // namespace beast::demo::event
