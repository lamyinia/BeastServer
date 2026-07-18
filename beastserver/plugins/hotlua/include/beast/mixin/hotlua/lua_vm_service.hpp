// LuaVmService：注册到 ServiceRegistry 的平台服务，供 gameplay 插件获取 Lua VM。
// 由 platform_hotlua 插件在 beast_platform_plugin_init 中创建并注册。

#pragma once

#include "beast/mixin/hotlua/lua_vm.hpp"

#include <memory>

namespace boost::asio {
class io_context;
} // namespace boost::asio

namespace beast::mixin::hotlua {

class LuaVmService {
public:
    LuaVmService();
    ~LuaVmService();

    LuaVmService(const LuaVmService&) = delete;
    LuaVmService& operator=(const LuaVmService&) = delete;

    // 创建一个独立的 LuaVm 实例（通常一个 engine 实例一个）。
    // 已注册默认 C++ 绑定（log / echo / version）。
    [[nodiscard]] std::unique_ptr<LuaVm> create_vm();

    // 注入共享 io_context（供后续 Phase 6 的 timer 绑定使用）。
    void set_io_context(boost::asio::io_context* io) noexcept { io_ = io; }
    [[nodiscard]] boost::asio::io_context* io_context() const noexcept { return io_; }

private:
    boost::asio::io_context* io_{nullptr};
};

} // namespace beast::mixin::hotlua
