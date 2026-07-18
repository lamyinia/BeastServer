// demo_hotlua 引擎：持有独立 LuaVm，加载脚本并响应 hotlua.run / hotlua.reload 事件。
//
// 设计要点：
// - EventDriven 模式：仅在事件到达时调用 Lua，无 tick 开销。
// - 每个 engine 实例独占一个 LuaVm（独立 sol::state），互不干扰。
// - 脚本路径优先读 BEAST_HOTLUA_SCRIPT 环境变量，缺省 "scripts/hotlua/main.lua"。
// - 脚本加载失败不抛异常，仅记录错误；后续 run 调用返回 false + last_error。
// - 回包走 ctx.send（player_id 非空时）；player_id 为空时仅记录日志。

#pragma once

#include "beast/platform/engine/instance/i_engine.hpp"
#include "beast/mixin/hotlua/lua_vm.hpp"
#include "beast/mixin/hotlua/lua_vm_service.hpp"

#include <memory>
#include <string>

namespace beast::demo::hotlua {

class HotluaEngine final : public beast::platform::engine::instance::IEngine {
public:
    // service 用于 create_vm；script_path 为空时走环境变量/默认路径。
    HotluaEngine(
        std::shared_ptr<beast::mixin::hotlua::LuaVmService> service,
        std::string script_path = {});

    void on_start(beast::platform::engine::context::EngineContext& ctx) override;
    void on_event(const beast::platform::engine::instance::InstanceEvent& event) override;

    [[nodiscard]] bool script_loaded() const noexcept { return script_loaded_; }
    [[nodiscard]] const std::string& script_path() const noexcept { return script_path_; }
    [[nodiscard]] std::string last_error() const noexcept;

private:
    // 解析最终脚本路径：构造参数 > 环境变量 > 默认。
    [[nodiscard]] static std::string resolve_script_path(const std::string& hint);

    // 处理 demo.hotlua.run：调用 Lua 函数并回包
    void on_run(
        const beast::platform::PlayerId& player_id,
        std::uint64_t client_seq,
        const std::vector<std::uint8_t>& payload);

    // 处理 demo.hotlua.reload：重载脚本
    void on_reload(
        const beast::platform::PlayerId& player_id,
        std::uint64_t client_seq);

    void send_run_response(
        const beast::platform::PlayerId& player_id,
        std::uint64_t client_seq,
        bool ok,
        const std::string& result);

    void send_reload_response(
        const beast::platform::PlayerId& player_id,
        std::uint64_t client_seq,
        bool ok,
        const std::string& message);

    beast::platform::engine::context::EngineContext* ctx_{nullptr};
    std::shared_ptr<beast::mixin::hotlua::LuaVmService> service_;
    std::unique_ptr<beast::mixin::hotlua::LuaVm> vm_;
    std::string script_path_;
    bool script_loaded_{false};
};

[[nodiscard]] std::unique_ptr<HotluaEngine> make_hotlua_engine(
    std::shared_ptr<beast::mixin::hotlua::LuaVmService> service,
    std::string script_path = {});

} // namespace beast::demo::hotlua
