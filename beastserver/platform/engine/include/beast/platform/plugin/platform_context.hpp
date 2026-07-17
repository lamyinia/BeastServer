#pragma once

#include "beast/platform/core/config/server_config.hpp"
#include "beast/platform/plugin/service_registry.hpp"

#include <boost/asio.hpp>

#include <string>

namespace beast::platform::engine::plugin {
class PluginHost;
}

namespace beast::platform::plugin {

// 平台插件初始化门面 — 类似 ServerContext，但面向平台服务（AiService / DB / Voice 等）。
// 生命周期限于 beast_platform_plugin_init 调用期间及之后只读注册结果。
//
// 与 ServerContext 的区别：
// - ServerContext 注册引擎/路由/biz table，面向玩法层
// - PlatformContext 注册共享服务，面向平台基础设施层
//
// 与 ServerContext 一样是栈对象：PluginHost::invoke_platform_plugin 在调用
// beast_platform_plugin_init 时构造，调用结束析构。插件若需长期持有 ctx
// 引用，应只读使用并复制所需原始指针（如 io_context、config），不要保存 ctx 本身。
class PlatformContext {
public:
    PlatformContext(
        std::string plugin_name,
        engine::plugin::PluginHost* host,
        ServiceRegistry* registry,
        boost::asio::io_context* io_context,
        const core::config::ServerConfig* config)
        : plugin_name_(std::move(plugin_name))
        , host_(host)
        , registry_(registry)
        , io_context_(io_context)
        , config_(config) {}

    [[nodiscard]] const std::string& plugin_name() const noexcept { return plugin_name_; }

    // 注册共享服务实例：service 通过 shared_ptr 共享所有权，registry 持有引用。
    // 模板参数 T 为服务接口类型，查询时需用相同 T。
    template<typename T>
    void register_service(std::string name, std::shared_ptr<T> service) {
        if (registry_) {
            registry_->register_service<T>(std::move(name), std::move(service));
        }
    }

    // 查询已注册的服务（跨 plugin 共享）。
    template<typename T>
    [[nodiscard]] std::shared_ptr<T> get_service(const std::string& name) const {
        if (!registry_) {
            return nullptr;
        }
        return registry_->get_service<T>(name);
    }

    [[nodiscard]] boost::asio::io_context* io_context() const noexcept { return io_context_; }
    [[nodiscard]] const core::config::ServerConfig* config() const noexcept { return config_; }
    [[nodiscard]] ServiceRegistry* registry() const noexcept { return registry_; }

private:
    std::string plugin_name_;
    engine::plugin::PluginHost* host_{nullptr};
    ServiceRegistry* registry_{nullptr};
    boost::asio::io_context* io_context_{nullptr};
    const core::config::ServerConfig* config_{nullptr};
};

} // namespace beast::platform::plugin
