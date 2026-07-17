#pragma once

#include "beast/platform/core/types.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_map>

namespace beast::platform::net::outbound {

/// 出站路由可靠性级别。插件 init 时通过 declare_outbound_route 声明。
enum class RouteReliability : std::uint8_t {
    /// 默认：走 KCP ikcp_send / TCP 可靠通道（现有行为）。
    Reliable = 0,
    /// 旁路：走 KCP unreliable 子通道（magic demux，latest-wins，无重传）。
    /// 仅对 KCP 通道生效；TCP 通道忽略此标记，仍走可靠路径。
    Unreliable = 1,
};

/**
 * 出站路由可靠性注册表。
 *
 * 生命周期：
 *   - 写入阶段：插件 init 时调 declare()（单线程，server 启动前）
 *   - 读取阶段：运行时 OutboundHub::send 查 reliability_of()，接收侧查 route_name_for()（多 IO 线程）
 *
 * route_id 编码：frame header 中 route_id 为 uint16，用 FNV-1a 16-bit hash 从 RouteId(string) 计算。
 * 无状态 hash 保证 server/client 独立计算结果一致，无需共享注册表。
 * 注册表同时维护 uint16→string 反查表（仅 server 侧接收 demux 用）。
 *
 * 线程安全：shared_mutex 保护，declare 独占写，lookup 共享读。
 */
class OutboundRouteRegistry {
public:
    /// 声明 route 的可靠性。重复声明以最后一次为准。同时填充 hash 反查表。
    void declare(RouteId route, RouteReliability reliability) {
        const std::uint16_t hash = route_id_hash(route);
        std::unique_lock lock(mutex_);
        reliability_map_[route] = reliability;
        reverse_hash_map_[hash] = route;
    }

    /// 查询 route 的可靠性。未声明的 route 默认 Reliable。
    [[nodiscard]] RouteReliability reliability_of(const RouteId& route) const {
        std::shared_lock lock(mutex_);
        const auto it = reliability_map_.find(route);
        if (it != reliability_map_.end()) {
            return it->second;
        }
        return RouteReliability::Reliable;
    }

    /// 便捷查询：route 是否为 Unreliable。
    [[nodiscard]] bool is_unreliable(const RouteId& route) const {
        return reliability_of(route) == RouteReliability::Unreliable;
    }

    /// route_name → uint16 (FNV-1a 16-bit hash)，用于 frame encode。
    /// 无状态纯函数，server/client 可独立计算，无需共享注册表。
    [[nodiscard]] static std::uint16_t route_id_hash(const RouteId& route) {
        // FNV-1a 32-bit，再 fold 到 16-bit
        std::uint32_t hash = 0x811C9DC5u; // FNV offset basis
        for (char c : route) {
            hash ^= static_cast<std::uint8_t>(c);
            hash *= 0x01000193u; // FNV prime
        }
        // XOR-fold 到 16-bit，减少碰撞
        return static_cast<std::uint16_t>(hash ^ (hash >> 16));
    }

    /// uint16 → route_name 反查（接收侧 demux 后反查 route 用）。
    /// 仅注册过的 route 可反查；未声明返回 nullopt。
    [[nodiscard]] std::optional<RouteId> route_name_for(std::uint16_t id) const {
        std::shared_lock lock(mutex_);
        const auto it = reverse_hash_map_.find(id);
        if (it != reverse_hash_map_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<RouteId, RouteReliability> reliability_map_;
    std::unordered_map<std::uint16_t, RouteId> reverse_hash_map_;
};

} // namespace beast::platform::net::outbound
