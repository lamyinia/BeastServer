#include "beast/platform/core/config/config_registry.hpp"
#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/server/game_server.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_running{true};
// SIGHUP 触发 TLS 证书热重载：handler 仅置 flag（async-signal-safe），
// 实际 reload 在主循环中执行（避免在 signal handler 里调用非 async-signal-safe 函数）。
std::atomic<bool> g_reload_tls_requested{false};

void on_terminate_signal(int /*signum*/) {
    g_running.store(false, std::memory_order_relaxed);
}

void on_reload_signal(int /*signum*/) {
    g_reload_tls_requested.store(true, std::memory_order_relaxed);
}

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " [config/server.json]\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string config_path = "config/server.json";
    if (argc > 2) {
        print_usage(argv[0]);
        return 1;
    }
    if (argc == 2) {
        config_path = argv[1];
    }
    config_path = beast::platform::core::config::resolve_config_file_path(config_path);

    const auto config_result = beast::platform::core::config::load_server_config_from_file(config_path);
    if (!config_result.ok()) {
        std::cerr << "Failed to load config: " << config_result.error().to_string() << '\n';
        return 1;
    }

    beast::platform::core::init_log(config_result.value().log.to_log_options());

    beast::platform::server::GameServerOptions options;
    options.config_file_path = config_path;

    beast::platform::server::GameServer server(std::move(config_result.value()), options);

    std::signal(SIGINT, on_terminate_signal);
    std::signal(SIGTERM, on_terminate_signal);
    // SIGHUP 默认动作是终止进程，必须显式注册 handler 才能用它触发证书热重载。
    std::signal(SIGHUP, on_reload_signal);

    server.start();

    while (g_running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (g_reload_tls_requested.exchange(false, std::memory_order_relaxed)) {
            BEAST_LOG_INFO("SIGHUP received, reloading TLS certificate");
            if (!server.reload_tls_cert()) {
                BEAST_LOG_WARN("TLS certificate reload failed or TLS disabled");
            }
        }
    }

    server.stop();
    return 0;
}
