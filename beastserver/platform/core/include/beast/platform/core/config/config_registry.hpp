#pragma once

#include "beast/platform/core/config/server_config.hpp"
#include "beast/platform/core/error.hpp"

#include <string>

namespace beast::platform::core::config {

class ConfigRegistry {
public:
    static ConfigRegistry& instance();

    [[nodiscard]] Result<ServerConfig> load_server_from_file(const std::string& path);
    void load_server_or_default(const std::string& path) noexcept;

    [[nodiscard]] const ServerConfig& server() const noexcept { return server_; }
    [[nodiscard]] bool loaded() const noexcept { return loaded_; }

    void apply_log() const;

    ConfigRegistry(const ConfigRegistry&) = delete;
    ConfigRegistry& operator=(const ConfigRegistry&) = delete;

private:
    ConfigRegistry() = default;

    ServerConfig server_{};
    bool loaded_{false};
};

[[nodiscard]] Result<ServerConfig> load_server_config_from_file(const std::string& path);

// 将相对 config 路径解析为绝对路径（支持从 build/ 目录启动的 IDE 场景）。
[[nodiscard]] std::string resolve_config_file_path(const std::string& path);

} // namespace beast::platform::core::config

