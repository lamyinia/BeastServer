#include "beast/platform/bizutil/config/store.hpp"

#include "beast/platform/bizutil/config/loader.hpp"
#include "beast/platform/core/log/logger.hpp"

#include <google/protobuf/message.h>

namespace beast::platform::bizutil::config {
namespace {

[[nodiscard]] std::filesystem::path resolve_table_path(
    const BizPaths& paths,
    const Manifest* manifest,
    const BizTableRegistration& registration) {
    if (manifest != nullptr) {
        if (const TableManifestEntry* entry = manifest->find(registration.logical_name)) {
            return paths.server_dir / entry->file;
        }
    }

    return paths.server_dir / (registration.logical_name + "_s.pb");
}

} // namespace

LoadResult BizConfigStore::load(
    const BizPaths& paths,
    const std::vector<BizTableRegistration>& registrations,
    LoadOptions options) {
    tables_.clear();
    table_views_.clear();
    manifest_.reset();
    loaded_ = false;

    if (registrations.empty()) {
        loaded_ = true;
        BEAST_LOG_INFO("BizConfig skipped: no registered tables");
        return LoadResult::success();
    }

    if (!std::filesystem::exists(paths.server_dir)) {
        LoadResult result;
        result.add_error(BizError{
            .table = {},
            .path = paths.server_dir.string(),
            .message = "bizconfig server dir not found",
        });
        return result;
    }

    Manifest manifest;
    const Manifest* manifest_ptr = nullptr;
    if (options.use_manifest && std::filesystem::exists(paths.manifest_path)) {
        std::string error_message;
        if (!Manifest::parse_file(paths.manifest_path, manifest, &error_message)) {
            LoadResult result;
            result.add_error(BizError{
                .table = {},
                .path = paths.manifest_path.string(),
                .message = error_message,
            });
            return result;
        }
        manifest_ = std::move(manifest);
        manifest_ptr = &*manifest_;
    } else if (options.use_manifest && options.fail_on_missing) {
        LoadResult result;
        result.add_error(BizError{
            .table = {},
            .path = paths.manifest_path.string(),
            .message = "manifest not found",
        });
        return result;
    }

    LoadResult result = LoadResult::success();
    for (const auto& registration : registrations) {
        if (registration.logical_name.empty() || !registration.factory) {
            result.add_error(BizError{
                .table = registration.logical_name,
                .path = {},
                .message = "invalid table registration",
            });
            continue;
        }

        const auto table_path = resolve_table_path(paths, manifest_ptr, registration);
        if (!std::filesystem::exists(table_path)) {
            result.add_error(BizError{
                .table = registration.logical_name,
                .path = table_path.string(),
                .message = "table file not found",
            });
            continue;
        }

        auto message = registration.factory();
        if (!message) {
            result.add_error(BizError{
                .table = registration.logical_name,
                .path = table_path.string(),
                .message = "table factory returned null",
            });
            continue;
        }

        BizError load_error;
        if (!Loader::load_pb_file(table_path, *message, &load_error)) {
            load_error.table = registration.logical_name;
            result.add_error(std::move(load_error));
            continue;
        }

        tables_.emplace(registration.logical_name, std::move(message));
    }

    if (!result.errors.empty() && options.fail_on_missing) {
        result.ok = false;
        return result;
    }

    if (tables_.empty() && options.fail_on_missing) {
        result.add_error(BizError{
            .table = {},
            .path = paths.server_dir.string(),
            .message = "no biz tables loaded",
        });
        return result;
    }

    loaded_ = true;
    result.ok = true;

    const char* version = manifest_ptr != nullptr ? manifest_ptr->version().data() : "unknown";
    BEAST_LOG_INFO(
        "BizConfig loaded version={} tables={} dir={}",
        version,
        tables_.size(),
        paths.server_dir.string());
    return result;
}

const Manifest* BizConfigStore::manifest() const noexcept {
    if (!manifest_.has_value()) {
        return nullptr;
    }
    return &*manifest_;
}

bool BizConfigStore::contains(const std::string_view logical_name) const noexcept {
    return tables_.contains(std::string(logical_name));
}

const google::protobuf::Message& BizConfigStore::require_message(
    const std::string_view logical_name) const {
    const google::protobuf::Message* message = find_message(logical_name);
    if (message == nullptr) {
        throw std::runtime_error("biz config table not found: " + std::string(logical_name));
    }
    return *message;
}

const google::protobuf::Message* BizConfigStore::find_message(
    const std::string_view logical_name) const noexcept {
    const auto it = tables_.find(std::string(logical_name));
    if (it == tables_.end()) {
        return nullptr;
    }
    return it->second.get();
}

} // namespace beast::platform::bizutil::config
