#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace beast::platform::net::transport {

/// 旁路不可靠帧的默认 magic（2 字节，big-endian 编码到 wire）。
/// 选 0xBEEF 是为了避开 KCP 默认 conv=1（wire 首字节为 0x00 0x00）的冲突；
/// 用户自定义 conv 时需保证 conv 的高 2 字节不与 magic 重合，否则 demux 会误判。
constexpr std::uint16_t kUnreliableFrameMagic = 0xBEEF;

/// 旁路帧 header 固定 8 字节：magic(2) + route_id(2) + seq(4)，所有多字节字段均 big-endian。
constexpr std::size_t kUnreliableFrameHeaderSize = 8;

/// 旁路不可靠帧：transport 层 demux 后的路由单位。
/// - magic：与 KCP 报文区分的前缀（2 字节 BE）
/// - route_id：业务路由 id（OutboundHub encode 时填入，接收侧反查路由表）
/// - seq：单调递增序号，用于接收侧 latest-wins 过滤（uint32 不回绕）
/// - payload：protobuf 序列化后的业务消息
struct UnreliableFrame {
    std::uint16_t magic{kUnreliableFrameMagic};
    std::uint16_t route_id{0};
    std::uint32_t seq{0};
    std::vector<std::uint8_t> payload;
};

/// 判断 buffer 前 2 字节是否为 unreliable frame magic。
/// 用于 KcpTransport::inject_inbound / do_read 中与 KCP 报文 demux。
/// magic 参数允许调用方传入自定义 magic（默认为 kUnreliableFrameMagic）。
inline bool is_unreliable_frame(const std::uint8_t* data, std::size_t size,
                                std::uint16_t magic = kUnreliableFrameMagic) {
    if (size < 2) {
        return false;
    }
    const std::uint16_t m = (static_cast<std::uint16_t>(data[0]) << 8) |
                            static_cast<std::uint16_t>(data[1]);
    return m == magic;
}

inline bool is_unreliable_frame(const std::vector<std::uint8_t>& data,
                                std::uint16_t magic = kUnreliableFrameMagic) {
    return is_unreliable_frame(data.data(), data.size(), magic);
}

/// 编码 UnreliableFrame 为 wire 字节流（magic + route_id + seq + payload，全 BE）。
/// 调用方负责在 send_unreliable 前完成 encode。
inline std::vector<std::uint8_t> encode_unreliable_frame(const UnreliableFrame& frame) {
    std::vector<std::uint8_t> out;
    out.reserve(kUnreliableFrameHeaderSize + frame.payload.size());

    // magic (BE)
    out.push_back(static_cast<std::uint8_t>((frame.magic >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(frame.magic & 0xFF));
    // route_id (BE)
    out.push_back(static_cast<std::uint8_t>((frame.route_id >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(frame.route_id & 0xFF));
    // seq (BE)
    out.push_back(static_cast<std::uint8_t>((frame.seq >> 24) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((frame.seq >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((frame.seq >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(frame.seq & 0xFF));
    // payload
    out.insert(out.end(), frame.payload.begin(), frame.payload.end());
    return out;
}

/// 从 wire 字节流解码 UnreliableFrame。
/// 不做 magic 校验（调用方应先 is_unreliable_frame 判定），仅按 8 字节 header 解析。
/// 返回 nullopt 表示 buffer 不足以容纳 header。
inline std::optional<UnreliableFrame> decode_unreliable_frame(
    const std::uint8_t* data, std::size_t size) {
    if (size < kUnreliableFrameHeaderSize) {
        return std::nullopt;
    }
    UnreliableFrame frame;
    frame.magic = (static_cast<std::uint16_t>(data[0]) << 8) |
                  static_cast<std::uint16_t>(data[1]);
    frame.route_id = (static_cast<std::uint16_t>(data[2]) << 8) |
                     static_cast<std::uint16_t>(data[3]);
    frame.seq = (static_cast<std::uint32_t>(data[4]) << 24) |
                (static_cast<std::uint32_t>(data[5]) << 16) |
                (static_cast<std::uint32_t>(data[6]) << 8) |
                static_cast<std::uint32_t>(data[7]);
    frame.payload.assign(data + kUnreliableFrameHeaderSize, data + size);
    return frame;
}

inline std::optional<UnreliableFrame> decode_unreliable_frame(
    const std::vector<std::uint8_t>& data) {
    return decode_unreliable_frame(data.data(), data.size());
}

} // namespace beast::platform::net::transport
