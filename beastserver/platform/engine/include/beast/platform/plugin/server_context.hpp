#pragma once

#include "beast/platform/core/types.hpp"
#include "beast/platform/bizutil/config/registration.hpp"
#include "beast/platform/engine/instance/engine_descriptor.hpp"
#include "beast/platform/engine/instance/instance_event.hpp"
#include "beast/platform/net/channel/channel_handler_context.hpp"
#include "beast/platform/net/channel/message.hpp"
#include "beast/platform/net/dispatch/router.hpp"

#include <google/protobuf/message.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace beast::platform::engine::dispatch {
class PlayerInstanceRegistry;
}

namespace beast::platform::engine::instance {
class InstanceManager;
}

namespace beast::platform::engine::plugin {
class PluginHost;
}

namespace beast::platform::net::session {
class SessionManager;
}

namespace beast::platform::plugin {

// 插件初始化时可调用的平台 API；生命周期限于 beast_plugin_init 调用期间及之后只读注册结果。
class ServerContext {
public:
    ServerContext(
        PluginName plugin_name,
        engine::plugin::PluginHost* host,
        engine::instance::InstanceManager* instance_manager,
        net::session::SessionManager* session_manager,
        engine::dispatch::PlayerInstanceRegistry* player_registry = nullptr);

    [[nodiscard]] const PluginName& plugin_name() const noexcept { return plugin_name_; }

    bool register_engine(engine::instance::EngineDescriptor descriptor);
    void register_route(RouteId route, net::dispatch::RouteHandler handler);

    template<typename ConfigMsg>
    void register_biz_table(std::string logical_name) {
        register_biz_table(
            std::move(logical_name),
            []() { return std::make_unique<ConfigMsg>(); });
    }

    void register_biz_table(
        std::string logical_name,
        std::function<std::unique_ptr<google::protobuf::Message>()> factory);

    bool create_instance(
        EngineName engine_name,
        InstanceId instance_id,
        std::vector<PlayerId> player_ids = {});

    [[nodiscard]] engine::instance::InstanceManager& instances() noexcept;

    // 查询玩家所在实例：Session 优先，Registry 作冷路径 fallback。
    [[nodiscard]] InstanceId instance_id_for(const PlayerId& player_id) const;

    // 局内事件投递：只读连接 ctx 缓存（auth 时已从 Registry 同步到 Session + pipeline）。
    // 注意：本方法不依赖 ServerContext 其余成员，仅用 instance_manager_，
    // 故路由 handler 应捕获 instance_manager_ 原始指针后转调静态重载，
    // 避免 ServerContext 作为局部对象析构后留下悬空引用。
    bool submit_instance_event(
        net::channel::ChannelHandlerContext& ch_ctx,
        const net::channel::MessagePtr& msg,
        RouteId engine_route,
        std::vector<std::uint8_t> payload);

    // 静态重载：供路由 handler 长期持有 instance_manager 原始指针后调用，
    // 避免捕获短生命周期的 ServerContext 引用。
    static bool submit_instance_event(
        engine::instance::InstanceManager* instance_manager,
        net::channel::ChannelHandlerContext& ch_ctx,
        const net::channel::MessagePtr& msg,
        RouteId engine_route,
        std::vector<std::uint8_t> payload);

    [[nodiscard]] engine::instance::InstanceManager* instance_manager_ptr() const noexcept {
        return instance_manager_;
    }

private:
    PluginName plugin_name_;
    engine::plugin::PluginHost* host_{nullptr};
    engine::instance::InstanceManager* instance_manager_{nullptr};
    net::session::SessionManager* session_manager_{nullptr};
    engine::dispatch::PlayerInstanceRegistry* player_registry_{nullptr};
};

} // namespace beast::platform::plugin
