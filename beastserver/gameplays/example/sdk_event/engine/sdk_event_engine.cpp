#include "engine/sdk_event_engine.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/context/engine_context.hpp"
#include "beast/platform/engine/instance/instance_event_dispatch.hpp"
#include "beast/platform/net/outbound/protocol_preference.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace beast::demo::event {

namespace {

// 单帧 envelope + frame 最大字节数（与 codec MAX_FRAME_BYTES 对齐）。
// 服务端构造 big echo 时不允许超过此值，否则客户端拆帧失败会断连。
// 预留 ~5KB 给 envelope / frame header / protobuf wire 开销。
constexpr std::uint32_t kMaxBigEchoBytes = 60 * 1024;

// 字符串 -> ProtocolPreference；对应 net::outbound::ProtocolPreference 枚举：
//   "" / "any"            -> Any          (任选 active channel)
//   "prefer_tcp"          -> PreferTcp    (优先 TCP，无可用 fallback)
//   "prefer_kcp"          -> PreferKcp
//   "prefer_websocket"    -> PreferWebsocket
//   "only_tcp"            -> TcpOnly      (严格 TCP，无可用时 NoChannel 不发)
//   "only_kcp"            -> KcpOnly
//   "only_websocket"      -> WebsocketOnly
// 未知值视为 Any（容错，避免客户端打错字导致服务端拒绝）。
beast::platform::net::outbound::ProtocolPreference parse_preference(const std::string& s) {
    using beast::platform::net::outbound::ProtocolPreference;
    if (s == "prefer_tcp")       return ProtocolPreference::PreferTcp;
    if (s == "prefer_kcp")       return ProtocolPreference::PreferKcp;
    if (s == "prefer_websocket") return ProtocolPreference::PreferWebsocket;
    if (s == "only_tcp")          return ProtocolPreference::TcpOnly;
    if (s == "only_kcp")          return ProtocolPreference::KcpOnly;
    if (s == "only_websocket")    return ProtocolPreference::WebsocketOnly;
    return ProtocolPreference::Any;  // "" / "any" / 未知
}

} // namespace

void SdkEventEngine::on_start(beast::platform::engine::context::EngineContext& ctx) {
    ctx_ = &ctx;
    BEAST_LOG_INFO("sdk_event engine started instance={}", ctx.instance_id());
}

void SdkEventEngine::on_event(const beast::platform::engine::instance::InstanceEvent& event) {
    BEAST_LOG_INFO("sdk_event player_id={} route={}",
        event.player_id,
        event.route);

    BEAST_ENGINE_EVENT_SWITCH(event)
        BEAST_ENGINE_EVENT_PROTO_PLAYER_THIS("sdk.echo", beast::demo::EchoRequest, on_echo)
        BEAST_ENGINE_EVENT_PROTO_THIS("sdk.echo.seq", beast::demo::SeqEchoRequest, on_seq_echo)
        BEAST_ENGINE_EVENT_PROTO_PLAYER_THIS("sdk.echo.bytes", beast::demo::BytesEchoRequest, on_bytes_echo)
        BEAST_ENGINE_EVENT_PROTO_PLAYER_THIS("sdk.echo.big", beast::demo::BigEchoRequest, on_big_echo)
        BEAST_ENGINE_EVENT_PROTO_THIS("sdk.transport.info", beast::demo::TransportInfoRequest, on_transport_info)
    BEAST_ENGINE_EVENT_SWITCH_END
}

void SdkEventEngine::on_echo(
    const beast::platform::PlayerId& player_id,
    const beast::demo::EchoRequest& req) {
    beast::demo::EchoResponse resp;
    resp.set_text(req.text());

    BEAST_LOG_INFO(
        "sdk_event echo from={} text_len={}",
        player_id,
        req.text().size());

    ctx_->send(player_id, "sdk.echo.resp", resp);
}

void SdkEventEngine::on_seq_echo(
    const beast::platform::engine::instance::InstanceEvent& event,
    const beast::demo::SeqEchoRequest& req) {
    beast::demo::SeqEchoResponse resp;
    resp.set_client_seq(event.client_seq);
    resp.set_text(req.text());

    BEAST_LOG_INFO(
        "sdk_event seq_echo from={} client_seq={} text_len={}",
        event.player_id,
        event.client_seq,
        req.text().size());

    // 回包带 client_seq，客户端可按 seq 配对 req/resp（区分并发请求）
    ctx_->send(event.player_id, "sdk.echo.seq.resp", resp, event.client_seq);
}

void SdkEventEngine::on_bytes_echo(
    const beast::platform::PlayerId& player_id,
    const beast::demo::BytesEchoRequest& req) {
    beast::demo::BytesEchoResponse resp;
    resp.set_payload(req.payload());  // 原样回显，验证二进制透传

    BEAST_LOG_INFO(
        "sdk_event bytes_echo from={} payload_len={}",
        player_id,
        req.payload().size());

    ctx_->send(player_id, "sdk.echo.bytes.resp", resp);
}

void SdkEventEngine::on_big_echo(
    const beast::platform::PlayerId& player_id,
    const beast::demo::BigEchoRequest& req) {
    const std::uint32_t size = req.size();

    beast::demo::BigEchoResponse resp;
    resp.set_size(size);

    if (size > kMaxBigEchoBytes) {
        // 越界：返回空 payload + size 字段保留请求值，客户端可识别为 reject
        BEAST_LOG_WARN(
            "sdk_event big_echo from={} size={} exceeds max={} clamped to 0",
            player_id,
            size,
            kMaxBigEchoBytes);
        ctx_->send(player_id, "sdk.echo.big.resp", resp);
        return;
    }

    // 用可预测的模式填充：byte[i] = i & 0xFF，客户端可校验完整性
    std::string payload(size, '\0');
    for (std::uint32_t i = 0; i < size; ++i) {
        payload[i] = static_cast<char>(i & 0xFF);
    }
    resp.set_payload(std::move(payload));

    BEAST_LOG_INFO("sdk_event big_echo from={} size={}", player_id, size);

    ctx_->send(player_id, "sdk.echo.big.resp", resp);
}

void SdkEventEngine::on_transport_info(
    const beast::platform::engine::instance::InstanceEvent& event,
    const beast::demo::TransportInfoRequest& req) {
    beast::demo::TransportInfoResponse resp;
    resp.set_requested_preference(req.preference());
    resp.set_player_id(event.player_id);

    const auto pref = parse_preference(req.preference());

    BEAST_LOG_INFO(
        "sdk_event transport_info from={} preference={} resolved_pref={}",
        event.player_id,
        req.preference(),
        static_cast<int>(pref));

    // 关键：用客户端指定的 preference 调度回包，走 OutboundHub 跨 channel 选路。
    // 客户端观察实际从哪个 channel 收到 resp，即可验证 preference 调度生效。
    //   - Prefer* 系列会 fallback 到 Any（无指定 channel 时）
    //   - Only*  系列严格匹配（无可用时 NoChannel，客户端不会收到 resp，也算一种验证）
    ctx_->send(event.player_id, "sdk.transport.info.resp", resp, event.client_seq, pref);
}

std::unique_ptr<SdkEventEngine> make_sdk_event_engine() {
    return std::make_unique<SdkEventEngine>();
}

} // namespace beast::demo::event
