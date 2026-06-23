#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace beast::platform::bizutil::config {

struct TableManifestEntry {
    std::string logical_name;
    std::filesystem::path file;
    std::string schema;
    std::optional<std::uint32_t> row_count;
    std::optional<std::string> sha256;
};

class Manifest {
public:
    [[nodiscard]] static bool parse_file(
        const std::filesystem::path& path,
        Manifest& out,
        std::string* error_message = nullptr);

    [[nodiscard]] std::string_view version() const noexcept { return version_; }
    [[nodiscard]] const TableManifestEntry* find(std::string_view logical_name) const;
    [[nodiscard]] const std::unordered_map<std::string, TableManifestEntry>& tables() const noexcept {
        return tables_;
    }

private:
    std::string version_;
    std::unordered_map<std::string, TableManifestEntry> tables_;
};

} // namespace beast::platform::bizutil::config
