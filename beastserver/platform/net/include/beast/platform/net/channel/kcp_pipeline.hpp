#pragma once

#include "beast/platform/net/channel/i_channel.hpp"

#include <cstdint>
#include <memory>

namespace beast::platform::net::channel {

class KcpCryptoHandler;

/// KCP 加密配置（与 core::config::KcpCryptoConfig 对齐，但独立于 config 层避免循环依赖）
struct KcpPipelineCryptoOptions {
    bool enabled{false};              // 是否启用 AEAD 加密
    std::uint16_t tag_bytes{16};      // GCM tag 长度 [8, 16]
    bool encrypt_bypass{true};        // 是否加密旁路不可靠帧（enabled=true 时生效）
};

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
    KcpPipelineCryptoOptions crypto;  // AEAD 加密配置
};

/// 与 TCP 一致：LengthField + Protobuf codec。KCP 可靠流式，分帧规则不变。
void install_kcp_transport_codec(const std::shared_ptr<IChannel>& channel, KcpPipelineOptions options = {});
void install_kcp_envelope_codec(const std::shared_ptr<IChannel>& channel);

/// 安装 KCP pipeline：crypto（可选）+ LengthField + Protobuf。
/// crypto.enabled=true 时在最底层（紧邻 transport）安装 KcpCryptoHandler 并返回该 handler，
/// 调用方（SessionManager）需将其传给 AuthHandler 以便 auth 成功后激活加密。
/// crypto.enabled=false 时返回 nullptr。
std::shared_ptr<KcpCryptoHandler> install_kcp_pipeline(
    const std::shared_ptr<IChannel>& channel,
    KcpPipelineOptions options = {});

} // namespace beast::platform::net::channel
