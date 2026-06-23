#include "beast/platform/bizutil/config/manifest.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

namespace beast::platform::bizutil::config {
namespace {

std::optional<std::uint32_t> read_row_count(const nlohmann::json& node) {
    if (!node.contains("row_count")) {
        return std::nullopt;
    }
    return node.at("row_count").get<std::uint32_t>();
}

std::optional<std::string> read_sha256(const nlohmann::json& node) {
    if (!node.contains("sha256")) {
        return std::nullopt;
    }
    const auto value = node.at("sha256").get<std::string>();
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

bool Manifest::parse_file(
    const std::filesystem::path& path,
    Manifest& out,
    std::string* error_message) {
    std::ifstream input(path);
    if (!input.is_open()) {
        if (error_message != nullptr) {
            *error_message = "open manifest failed: " + path.string();
        }
        return false;
    }

    try {
        nlohmann::json root;
        input >> root;

        out = Manifest{};
        if (root.contains("version")) {
            out.version_ = root.at("version").get<std::string>();
        }

        if (!root.contains("tables") || !root.at("tables").is_object()) {
            if (error_message != nullptr) {
                *error_message = "manifest missing object field: tables";
            }
            return false;
        }

        for (const auto& [logical_name, entry] : root.at("tables").items()) {
            if (!entry.is_object() || !entry.contains("file")) {
                if (error_message != nullptr) {
                    *error_message = "invalid manifest entry: " + logical_name;
                }
                return false;
            }

            TableManifestEntry table;
            table.logical_name = logical_name;
            table.file = entry.at("file").get<std::string>();
            if (entry.contains("schema")) {
                table.schema = entry.at("schema").get<std::string>();
            }
            table.row_count = read_row_count(entry);
            table.sha256 = read_sha256(entry);
            out.tables_.emplace(logical_name, std::move(table));
        }

        return true;
    } catch (const nlohmann::json::exception& ex) {
        if (error_message != nullptr) {
            *error_message = std::string("manifest json parse failed: ") + ex.what();
        }
        return false;
    }
}

const TableManifestEntry* Manifest::find(const std::string_view logical_name) const {
    const auto it = tables_.find(std::string(logical_name));
    if (it == tables_.end()) {
        return nullptr;
    }
    return &it->second;
}

} // namespace beast::platform::bizutil::config
