// LuaVmService 实现：创建 LuaVm 并注册默认 C++ 绑定。

#include "beast/mixin/hotlua/lua_vm_service.hpp"
#include "beast/mixin/hotlua/lua_bindings.hpp"

namespace beast::mixin::hotlua {

LuaVmService::LuaVmService() = default;
LuaVmService::~LuaVmService() = default;

std::unique_ptr<LuaVm> LuaVmService::create_vm() {
    auto vm = std::make_unique<LuaVm>();
    // 注册默认 C++ 绑定（log / echo / version 等）
    register_default_bindings(vm->state());
    return vm;
}

} // namespace beast::mixin::hotlua
