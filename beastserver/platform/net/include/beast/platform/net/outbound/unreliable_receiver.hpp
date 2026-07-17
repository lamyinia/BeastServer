#pragma once

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/net/channel/channel_pipeline.hpp"
#include "beast/platform/net/channel/message.hpp"
#include "beast/platform/net/outbound/route_reliability_registry.hpp"
#include "beast/platform/net/transport/unreliable_frame.hpp"

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace beast::platform::net::outbound {

/**
 * 旁路不可靠帧接收器：per-channel 实例，处理 transport demux 后的旁路帧。
 *
 * 职责链：
 *   1. decode_unreliable_frame 解析 8 字节 header + payload
 *   2. route_name_for(route_id) 反查 route_name（未声明 route 丢弃）
 *   3. latest-wins 过滤：per-route_id 维护 last_seq，seq <= last_seq 的旧/重复帧丢弃
 *   4. 构造 MessagePtr 并 fire_channel_read 到 pipeline（经 LengthField/Protobuf codec 透传到 RouterHandler）
 *
 * 线程安全：process 在 channel transport strand 上调用（与 on_transport_bytes 同上下文），
 * seq 状态用 mutex 保护以防 transport strand 与 session strand 不一致时的竞态。
 */
class UnreliableReceiver {
public:
    explicit UnreliableReceiver(std::shared_ptr<OutboundRouteRegistry> registry)
        : registry_(std::move(registry)) {}

    /// 处理收到的旁路帧（含 8 字节 header）。pipeline 引用由调用方保证有效（callback 生命周期 <= channel）。
    void process(channel::IChannel::Bytes&& frame_bytes, channel::ChannelPipeline& pipeline) {
        if (!registry_) {
            return;
        }

        auto decoded = transport::decode_unreliable_frame(frame_bytes);
        if (!decoded) {
            BEAST_LOG_WARN("UnreliableReceiver: decode failed (frame too small)");
            return;
        }

        if (decoded->magic != transport::kUnreliableFrameMagic) {
            BEAST_LOG_WARN("UnreliableReceiver: magic mismatch got={:#06x}", decoded->magic);
            return;
        }

        auto route_name = registry_->route_name_for(decoded->route_id);
        if (!route_name) {
            BEAST_LOG_WARN("UnreliableReceiver: unknown route_id {}", decoded->route_id);
            return;
        }

        // latest-wins 过滤：seq <= last_seq 的帧视为旧/重复，丢弃。
        {
            std::lock_guard lock(seq_mutex_);
            auto& last_seq = last_seq_[decoded->route_id];
            if (decoded->seq <= last_seq) {
                return;
            }
            last_seq = decoded->seq;
        }

        // 构造 MessagePtr 并 feed 到 pipeline。
        // MessagePtr 经 LengthFieldDecoder/ProtobufDecoder 透传（非 Bytes 类型直接 fire_channel_read），
        // 到达 AuthHandler → RouterHandler（auth 后 attach），最终分发到注册的 route handler。
        auto message = std::make_shared<channel::Message>();
        message->route = std::move(*route_name);
        message->payload = std::move(decoded->payload);
        pipeline.fire_channel_read(channel::MessagePtr(std::move(message)));
    }

private:
    std::shared_ptr<OutboundRouteRegistry> registry_;
    mutable std::mutex seq_mutex_;
    std::unordered_map<std::uint16_t, std::uint32_t> last_seq_;
};

} // namespace beast::platform::net::outbound
