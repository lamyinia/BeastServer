#pragma once

namespace beast::platform::plugin {

class ServerContext;

using PluginInitFn = void (*)(ServerContext& ctx);

} // namespace beast::platform::plugin

#if defined(_WIN32)
#define BEAST_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define BEAST_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#define BEAST_PLUGIN_INIT ::beast_plugin_init

// 插件侧路由工具：parse_payload / register_parsed_route / register_instance_route
#include "beast/platform/plugin/route_handler.hpp"

// 动态插件入口：plugins/<name>/plugin.cpp 中实现。
BEAST_PLUGIN_EXPORT void beast_plugin_init(beast::platform::plugin::ServerContext& ctx);
