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

void on_signal(int /*signum*/) {
    g_running.store(false, std::memory_order_relaxed);
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

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    server.start();

    while (g_running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    server.stop();
    return 0;
}
