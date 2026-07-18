#include "engine/hotlua_engine.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/context/engine_context.hpp"
#include "beast/platform/engine/instance/instance_event_dispatch.hpp"

#include "demo_hotlua.pb.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace beast::demo::hotlua {
namespace {

constexpr const char* kDefaultScriptPath = "scripts/hotlua/main.lua";
constexpr const char* kEnvScriptPath = "BEAST_HOTLUA_SCRIPT";

// 解析 InstanceEvent payload 为 RunRequest
[[nodiscard]] bool parse_run_request(
    const std::vector<std::uint8_t>& payload,
    beast::demo::hotlua::RunRequest& out) {
    return out.ParseFromArray(payload.data(), static_cast<int>(payload.size()));
}

} // namespace

HotluaEngine::HotluaEngine(
    std::shared_ptr<beast::mixin::hotlua::LuaVmService> service,
    std::string script_path)
    : service_(std::move(service))
    , script_path_(resolve_script_path(script_path)) {}

std::string HotluaEngine::resolve_script_path(const std::string& hint) {
    if (!hint.empty()) {
        return hint;
    }
    if (const char* env = std::getenv(kEnvScriptPath)) {
        if (env[0] != '\0') {
            return env;
        }
    }
    return kDefaultScriptPath;
}

std::string HotluaEngine::last_error() const noexcept {
    return vm_ ? vm_->last_error() : std::string{"vm not initialized"};
}

void HotluaEngine::on_start(beast::platform::engine::context::EngineContext& ctx) {
    ctx_ = &ctx;

    if (!service_) {
        BEAST_LOG_ERROR("demo_hotlua: LuaVmService unavailable, engine disabled");
        return;
    }

    vm_ = service_->create_vm();
    if (!vm_) {
        BEAST_LOG_ERROR("demo_hotlua: create_vm() returned null");
        return;
    }

    script_loaded_ = vm_->load_script(script_path_);
    if (script_loaded_) {
        BEAST_LOG_INFO("demo_hotlua: script loaded path={}", script_path_);
    } else {
        BEAST_LOG_WARN(
            "demo_hotlua: script load failed path={} err={}",
            script_path_,
            vm_->last_error());
    }
}

void HotluaEngine::on_event(const beast::platform::engine::instance::InstanceEvent& event) {
    BEAST_ENGINE_EVENT_SWITCH(event)
        BEAST_ENGINE_EVENT_ROUTE("demo.hotlua.run",
            on_run(event.player_id, event.client_seq, event.payload))
        BEAST_ENGINE_EVENT_ROUTE("demo.hotlua.reload",
            on_reload(event.player_id, event.client_seq))
    BEAST_ENGINE_EVENT_SWITCH_END
}

void HotluaEngine::on_run(
    const beast::platform::PlayerId& player_id,
    std::uint64_t client_seq,
    const std::vector<std::uint8_t>& payload) {

    if (!vm_) {
        const std::string err = "vm not initialized";
        send_run_response(player_id, client_seq, false, err);
        return;
    }
    if (!script_loaded_) {
        const std::string err = "script not loaded: " + vm_->last_error();
        send_run_response(player_id, client_seq, false, err);
        return;
    }

    beast::demo::hotlua::RunRequest request;
    if (!parse_run_request(payload, request)) {
        const std::string err = "parse RunRequest failed";
        send_run_response(player_id, client_seq, false, err);
        return;
    }

    const auto& fn_name = request.function_name();
    if (fn_name.empty()) {
        const std::string err = "function_name is empty";
        send_run_response(player_id, client_seq, false, err);
        return;
    }

    std::string result;
    const bool ok = vm_->call_function(fn_name, std::vector<std::string>{
        request.args().begin(), request.args().end()}, result);

    if (ok) {
        BEAST_LOG_INFO(
            "demo_hotlua run ok fn={} args={} result={}",
            fn_name, request.args().size(), result);
        send_run_response(player_id, client_seq, true, result);
    } else {
        const std::string err = vm_->last_error();
        BEAST_LOG_WARN("demo_hotlua run failed fn={} err={}", fn_name, err);
        send_run_response(player_id, client_seq, false, err);
    }
}

void HotluaEngine::on_reload(
    const beast::platform::PlayerId& player_id,
    std::uint64_t client_seq) {

    if (!vm_) {
        send_reload_response(player_id, client_seq, false, "vm not initialized");
        return;
    }

    const bool ok = vm_->reload();
    script_loaded_ = ok;
    const std::string msg = ok
        ? "reloaded path=" + script_path_
        : "reload failed: " + vm_->last_error();

    BEAST_LOG_INFO("demo_hotlua reload {} ok={}", msg, ok);
    send_reload_response(player_id, client_seq, ok, msg);
}

void HotluaEngine::send_run_response(
    const beast::platform::PlayerId& player_id,
    std::uint64_t client_seq,
    bool ok,
    const std::string& result) {
    if (!ctx_ || player_id.empty()) {
        return;
    }
    beast::demo::hotlua::RunResponse resp;
    resp.set_ok(ok);
    resp.set_result(result);
    ctx_->send(player_id, "demo.hotlua.run_resp", resp, client_seq);
}

void HotluaEngine::send_reload_response(
    const beast::platform::PlayerId& player_id,
    std::uint64_t client_seq,
    bool ok,
    const std::string& message) {
    if (!ctx_ || player_id.empty()) {
        return;
    }
    beast::demo::hotlua::ReloadResponse resp;
    resp.set_ok(ok);
    resp.set_message(message);
    ctx_->send(player_id, "demo.hotlua.reload_resp", resp, client_seq);
}

std::unique_ptr<HotluaEngine> make_hotlua_engine(
    std::shared_ptr<beast::mixin::hotlua::LuaVmService> service,
    std::string script_path) {
    return std::make_unique<HotluaEngine>(std::move(service), std::move(script_path));
}

} // namespace beast::demo::hotlua
