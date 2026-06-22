#include "beast/platform/core/config/server_config.hpp"

#include <algorithm>

namespace beast::platform::core::config {

bool PluginsConfig::should_load(std::string_view plugin_name) const {
    if (!auto_load) {
        return false;
    }

    const auto name = std::string(plugin_name);

    if (std::find(disable.begin(), disable.end(), name) != disable.end()) {
        return false;
    }

    if (!only.empty()) {
        return std::find(only.begin(), only.end(), name) != only.end();
    }

    return true;
}

} // namespace beast::platform::core::config
