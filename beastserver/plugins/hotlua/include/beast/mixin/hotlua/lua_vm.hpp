// Lua VM 包装：基于 sol2，提供脚本加载、函数调用、热重载。
// 每个 engine 实例持有一个 LuaVm，互不干扰。

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

// sol2 前向声明，避免头文件强依赖 sol2（sol::function 是 using 别名，不可前向声明）
namespace sol {
class state;
} // namespace sol

namespace beast::mixin::hotlua {

// 单个 Lua 虚拟机实例。
// 非线程安全：调用方（engine carrier 线程）需保证同一实例不并发调用。
class LuaVm {
public:
    LuaVm();
    ~LuaVm();

    LuaVm(const LuaVm&) = delete;
    LuaVm& operator=(const LuaVm&) = delete;
    LuaVm(LuaVm&&) noexcept;
    LuaVm& operator=(LuaVm&&) noexcept;

    // 加载脚本文件。成功后记录路径，供 reload 使用。
    // 返回 false 时 last_error() 含错误信息。
    [[nodiscard]] bool load_script(const std::string& path);

    // 热重载：销毁当前 lua_State，重建并重新加载上次 load_script 的文件。
    // Lua 状态全量重置（demo 阶段不保留状态）。
    [[nodiscard]] bool reload();

    // 调用 Lua 全局函数，参数和返回值均为字符串（JSON 编码，demo 阶段最简）。
    // 返回 false 时 last_error() 含错误信息。
    [[nodiscard]] bool call_function(
        const std::string& name,
        const std::vector<std::string>& args,
        std::string& result);

    // 调用无参 Lua 全局函数，返回字符串结果。
    [[nodiscard]] bool call_function(const std::string& name, std::string& result);

    // 注册 C++ 函数到 Lua 全局表（供脚本回调）。
    void register_function(const std::string& name, std::function<void(const std::string&)> fn);

    // 暴露底层 sol2 state，供高级用法（绑定更多类型等）。
    // 调用方需自行处理 sol2 类型。
    [[nodiscard]] sol::state& state() noexcept;
    [[nodiscard]] const sol::state& state() const noexcept;

    [[nodiscard]] std::string last_error() const noexcept { return last_error_; }
    [[nodiscard]] bool loaded() const noexcept { return !script_path_.empty(); }
    [[nodiscard]] const std::string& script_path() const noexcept { return script_path_; }

private:
    void reset();
    void open_libs();

    std::unique_ptr<sol::state> lua_;
    std::string script_path_;
    std::string last_error_;
};

} // namespace beast::mixin::hotlua
