#pragma once

namespace beast::platform::plugin {

class PlatformContext;

using PlatformPluginInitFn = void (*)(PlatformContext& ctx);

} // namespace beast::platform::plugin

#if defined(_WIN32)
#define BEAST_PLATFORM_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define BEAST_PLATFORM_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#define BEAST_PLATFORM_PLUGIN_INIT ::beast_platform_plugin_init

// 平台插件入口：plugins/platform/<name>/plugin.cpp 中实现。
// 由 PluginHost::load_platform_plugins() 在 GameServer 构造早期调用，
// 早于玩法插件 load_gameplay_plugins()，使平台服务在玩法层 init 前就绪。
BEAST_PLATFORM_PLUGIN_EXPORT void beast_platform_plugin_init(
    beast::platform::plugin::PlatformContext& ctx);
