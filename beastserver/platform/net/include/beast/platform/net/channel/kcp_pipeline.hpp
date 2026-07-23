#pragma once

#include "beast/platform/net/channel/i_channel.hpp"

#include <cstdint>
#include <memory>

namespace beast::platform::net::channel {

/**
 * KCP pipeline 选项。
 *
 * KCP 是可靠的流式 UDP（语义上接近 TCP），复用 LengthField + Protobuf codec。
 * 以下字段为 KCP 协议参数占位，待 ikcp 接入后由 KcpTransport 读取：
 *   - conv：会话 id，握手协商（预热阶段未使用）
 *   - snd_wnd / rcv_wnd：发送/接收窗口（默认 32/32，KCP 推荐）
 *   - nodelay：是否启用快速模式（1=开启）
 *   - interval：内部 update 间隔（ms）
 *   - resend：快速重传阈值
 *   - nc：是否关闭拥塞控制
 *
 * KCP 加密统一由 DTLS 在 UDP 层处理（生产环境强制 dtls.enabled=true），
 * pipeline 层不再安装应用层加密 handler。
 */
struct KcpPipelineOptions {
    std::uint32_t max_frame_bytes{64 * 1024};
    std::uint32_t conv{0};
    std::uint32_t snd_wnd{32};
    std::uint32_t rcv_wnd{32};
    std::uint32_t nodelay{1};
    std::uint32_t interval{10};
    std::uint32_t resend{2};
    std::uint32_t nc{1};
};

/// 与 TCP 一致：LengthField + Protobuf codec。KCP 可靠流式，分帧规则不变。
void install_kcp_transport_codec(const std::shared_ptr<IChannel>& channel, KcpPipelineOptions options = {});
void install_kcp_envelope_codec(const std::shared_ptr<IChannel>& channel);

/// 安装 KCP pipeline：LengthField + Protobuf。
/// KCP 加密由 DTLS 在 UDP 层处理（DtlsTransport），pipeline 层不安装应用层加密 handler。
void install_kcp_pipeline(
    const std::shared_ptr<IChannel>& channel,
    KcpPipelineOptions options = {});

} // namespace beast::platform::net::channel
