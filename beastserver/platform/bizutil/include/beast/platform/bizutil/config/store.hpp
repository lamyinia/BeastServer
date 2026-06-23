#pragma once

#include "beast/platform/bizutil/config/load_options.hpp"
#include "beast/platform/bizutil/config/manifest.hpp"
#include "beast/platform/bizutil/config/paths.hpp"
#include "beast/platform/bizutil/config/registration.hpp"
#include "beast/platform/bizutil/config/result.hpp"
#include "beast/platform/bizutil/config/table_view.hpp"

#include <any>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace google::protobuf {
class Message;
}

namespace beast::platform::bizutil::config {

class BizConfigStore {
public:
    [[nodiscard]] LoadResult load(
        const BizPaths& paths,
        const std::vector<BizTableRegistration>& registrations,
        LoadOptions options = {});

    [[nodiscard]] bool loaded() const noexcept { return loaded_; }
    [[nodiscard]] const Manifest* manifest() const noexcept;

    [[nodiscard]] bool contains(std::string_view logical_name) const noexcept;

    template<typename ConfigMsg>
    [[nodiscard]] const ConfigMsg& require(std::string_view logical_name) const {
        return cast_message<ConfigMsg>(require_message(logical_name));
    }

    template<typename ConfigMsg>
    [[nodiscard]] const ConfigMsg* find(std::string_view logical_name) const noexcept {
        const google::protobuf::Message* message = find_message(logical_name);
        if (message == nullptr) {
            return nullptr;
        }
        return &cast_message<ConfigMsg>(*message);
    }

    template<typename ConfigMsg, typename Row>
    [[nodiscard]] const Row& require_row_by_id(
        std::string_view logical_name,
        std::uint32_t id) const {
        const Row* row = find_row_by_id<ConfigMsg, Row>(logical_name, id);
        if (row == nullptr) {
            throw std::runtime_error(
                "biz config row not found: " + std::string(logical_name) + " id=" + std::to_string(id));
        }
        return *row;
    }

    template<typename ConfigMsg, typename Row>
    [[nodiscard]] const Row* find_row_by_id(
        std::string_view logical_name,
        std::uint32_t id) const noexcept {
        try {
            return table_view<ConfigMsg, Row>(logical_name).by_id().find(id);
        } catch (...) {
            return nullptr;
        }
    }

    template<typename ConfigMsg, typename Row>
    [[nodiscard]] const Row& require_row_by_index(
        std::string_view logical_name,
        std::string_view index) const {
        const Row* row = find_row_by_index<ConfigMsg, Row>(logical_name, index);
        if (row == nullptr) {
            throw std::runtime_error(
                "biz config row not found: " + std::string(logical_name) + " index=" + std::string(index));
        }
        return *row;
    }

    template<typename ConfigMsg, typename Row>
    [[nodiscard]] const Row* find_row_by_index(
        std::string_view logical_name,
        std::string_view index) const noexcept {
        try {
            return table_view<ConfigMsg, Row>(logical_name).by_key().find(index);
        } catch (...) {
            return nullptr;
        }
    }

private:
    [[nodiscard]] const google::protobuf::Message& require_message(std::string_view logical_name) const;
    [[nodiscard]] const google::protobuf::Message* find_message(std::string_view logical_name) const noexcept;

    template<typename ConfigMsg>
    [[nodiscard]] static const ConfigMsg& cast_message(const google::protobuf::Message& message) {
        const auto* typed = dynamic_cast<const ConfigMsg*>(&message);
        if (typed == nullptr) {
            throw std::runtime_error("biz config table type mismatch");
        }
        return *typed;
    }

    template<typename ConfigMsg, typename Row>
    [[nodiscard]] const BizTableView<ConfigMsg, Row>& table_view(std::string_view logical_name) const {
        const std::string key(logical_name);
        const auto it = table_views_.find(key);
        if (it != table_views_.end()) {
            return *std::any_cast<BizTableView<ConfigMsg, Row>>(&it->second);
        }

        const auto& table = require<ConfigMsg>(logical_name);
        const std::vector<Row> rows(table.rows().begin(), table.rows().end());
        const auto [inserted, _] = table_views_.emplace(
            key,
            BizTableView<ConfigMsg, Row>::bind(table, rows));
        return *std::any_cast<BizTableView<ConfigMsg, Row>>(&inserted->second);
    }

    bool loaded_{false};
    std::optional<Manifest> manifest_;
    std::unordered_map<std::string, std::unique_ptr<google::protobuf::Message>> tables_;
    mutable std::unordered_map<std::string, std::any> table_views_;
};

} // namespace beast::platform::bizutil::config
