#include "beast/platform/bizutil/config/paths.hpp"

#include "beast/platform/core/config/config_registry.hpp"

#include <cstdlib>
#include <filesystem>

namespace beast::platform::bizutil::config {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] fs::path repository_root_from_config_path(const std::string& config_file_path) {
    if (config_file_path.empty()) {
        return fs::current_path();
    }

    const fs::path config_path = config_file_path;
    const fs::path config_dir = config_path.parent_path();
    if (config_dir.filename() == "config") {
        return config_dir.parent_path();
    }
    return config_dir;
}

[[nodiscard]] fs::path first_existing_biz_dir(
    const fs::path& repo_root,
    const fs::path& configured_relative) {
    const fs::path candidates[] = {
        repo_root / "build/RelWithDebInfo/bizconfig/server",
        repo_root / configured_relative,
        fs::current_path() / "bizconfig/server",
        fs::path(core::config::resolve_config_file_path(configured_relative.string())),
    };

    for (const auto& candidate : candidates) {
        if (candidate.empty()) {
            continue;
        }
        if (fs::exists(candidate) && fs::is_directory(candidate)) {
            return candidate;
        }
    }

    return repo_root / configured_relative;
}

[[nodiscard]] fs::path resolve_manifest_path(
    const fs::path& server_dir,
    const std::string& manifest_file) {
    if (manifest_file.empty()) {
        return server_dir / "manifest.json";
    }

    // 导出布局：build/.../bizconfig/manifest.json + server/*.pb
    const fs::path beside_server = server_dir.parent_path() / manifest_file;
    if (fs::exists(beside_server)) {
        return beside_server;
    }

    // 兼容 manifest 与 .pb 同目录
    return server_dir / manifest_file;
}

} // namespace

BizPaths resolve_biz_paths(
    const core::config::BizConfigSettings& settings,
    const PathResolveOptions& options) {
    BizPaths paths;

    if (const char* env_dir = std::getenv("BEAST_BIZCONFIG_DIR")) {
        paths.server_dir = fs::path(env_dir);
        paths.manifest_path = resolve_manifest_path(paths.server_dir, settings.manifest_file);
        return paths;
    }

    if (fs::path(settings.dir).is_absolute()) {
        paths.server_dir = settings.dir;
        paths.manifest_path = resolve_manifest_path(paths.server_dir, settings.manifest_file);
        return paths;
    }

    const fs::path repo_root = repository_root_from_config_path(options.config_file_path);
    paths.server_dir = first_existing_biz_dir(repo_root, settings.dir).lexically_normal();
    paths.manifest_path = resolve_manifest_path(paths.server_dir, settings.manifest_file);
    return paths;
}

} // namespace beast::platform::bizutil::config
