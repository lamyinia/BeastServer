#pragma once

#include "beast/platform/core/config/server_config.hpp"

#include <filesystem>
#include <string>

namespace beast::platform::bizutil::config {

struct BizPaths {
    std::filesystem::path server_dir;
    std::filesystem::path manifest_path;
};

struct PathResolveOptions {
    std::string config_file_path;
};

[[nodiscard]] BizPaths resolve_biz_paths(
    const core::config::BizConfigSettings& settings,
    const PathResolveOptions& options = {});

} // namespace beast::platform::bizutil::config
