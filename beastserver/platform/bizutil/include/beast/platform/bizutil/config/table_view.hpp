#pragma once

#include "beast/platform/bizutil/config/load_options.hpp"
#include "beast/platform/bizutil/config/manifest.hpp"
#include "beast/platform/bizutil/config/paths.hpp"
#include "beast/platform/bizutil/config/registration.hpp"
#include "beast/platform/bizutil/config/result.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace beast::platform::bizutil::config {

template<typename Row>
class IdIndex {
public:
    using IdGetter = std::function<std::uint32_t(const Row&)>;

    void build(const std::vector<Row>& rows, IdGetter get_id) {
        by_id_.clear();
        for (const auto& row : rows) {
            by_id_.emplace(get_id(row), &row);
        }
    }

    [[nodiscard]] const Row* find(std::uint32_t id) const noexcept {
        const auto it = by_id_.find(id);
        return it == by_id_.end() ? nullptr : it->second;
    }

private:
    std::unordered_map<std::uint32_t, const Row*> by_id_;
};

template<typename Row>
class KeyIndex {
public:
    using KeyGetter = std::function<std::string_view(const Row&)>;

    void build(const std::vector<Row>& rows, KeyGetter get_key) {
        by_key_.clear();
        for (const auto& row : rows) {
            by_key_.emplace(std::string(get_key(row)), &row);
        }
    }

    [[nodiscard]] const Row* find(std::string_view key) const noexcept {
        const auto it = by_key_.find(std::string(key));
        return it == by_key_.end() ? nullptr : it->second;
    }

private:
    std::unordered_map<std::string, const Row*> by_key_;
};

template<typename ConfigMsg, typename Row>
class BizTableView {
public:
    [[nodiscard]] static BizTableView bind(
        const ConfigMsg& config,
        const std::vector<Row>& rows) {
        BizTableView view;
        view.config_ = &config;
        view.rows_ = rows;
        return view;
    }

    [[nodiscard]] const ConfigMsg& config() const noexcept { return *config_; }
    [[nodiscard]] const std::vector<Row>& rows() const noexcept { return rows_; }

    [[nodiscard]] const IdIndex<Row>& by_id() const {
        if (!id_built_) {
            id_index_.build(rows_, [](const Row& row) { return row.id(); });
            id_built_ = true;
        }
        return id_index_;
    }

    [[nodiscard]] const KeyIndex<Row>& by_key() const {
        if (!key_built_) {
            key_index_.build(rows_, [](const Row& row) { return row.index(); });
            key_built_ = true;
        }
        return key_index_;
    }

private:
    const ConfigMsg* config_{nullptr};
    std::vector<Row> rows_;
    mutable IdIndex<Row> id_index_;
    mutable KeyIndex<Row> key_index_;
    mutable bool id_built_{false};
    mutable bool key_built_{false};
};

} // namespace beast::platform::bizutil::config
