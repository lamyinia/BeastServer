#include "beast/mixin/dirtypersist/dirty_tracker.hpp"

#include <utility>

namespace beast::platform::dirtypersist {

void DirtyTracker::mark_field_dirty(std::string_view table, std::string_view id,
                                     std::string_view field, FieldValue value,
                                     std::string_view id_column) {
    auto key = make_key(table, id);
    auto it = dirty_.find(key);
    if (it == dirty_.end()) {
        FlushOp op;
        op.table     = std::string{table};
        op.id        = std::string{id};
        op.id_column = std::string{id_column};
        op.fields.emplace_back(std::string{field}, std::move(value));
        dirty_.emplace(std::move(key), std::move(op));
    } else {
        // 同一实体的 id_column 应该不变，保持首次注册的值
        auto& fields = it->second.fields;
        for (auto& [name, v] : fields) {
            if (name == field) {
                v = std::move(value);
                return;
            }
        }
        fields.emplace_back(std::string{field}, std::move(value));
    }
}

void DirtyTracker::mark_entity_dirty(std::string_view table, std::string_view id,
                                     std::vector<std::pair<std::string, FieldValue>> fields,
                                     std::string_view id_column) {
    auto key = make_key(table, id);
    FlushOp op;
    op.table     = std::string{table};
    op.id        = std::string{id};
    op.id_column = std::string{id_column};
    op.fields    = std::move(fields);
    dirty_[std::move(key)] = std::move(op);
}

std::vector<FlushOp> DirtyTracker::take_dirty() {
    std::vector<FlushOp> out;
    out.reserve(dirty_.size());
    for (auto& [_, op] : dirty_) {
        if (!op.empty()) {
            out.push_back(std::move(op));
        }
    }
    dirty_.clear();
    return out;
}

std::optional<FlushOp> DirtyTracker::take_one(std::string_view table, std::string_view id) {
    auto key = make_key(table, id);
    auto it = dirty_.find(key);
    if (it == dirty_.end()) {
        return std::nullopt;
    }
    FlushOp op = std::move(it->second);
    dirty_.erase(it);
    return op;
}

void DirtyTracker::clear() noexcept {
    dirty_.clear();
}

bool DirtyTracker::empty() const noexcept {
    return dirty_.empty();
}

std::size_t DirtyTracker::size() const noexcept {
    return dirty_.size();
}

std::size_t DirtyTracker::field_count() const noexcept {
    std::size_t total = 0;
    for (const auto& [_, op] : dirty_) {
        total += op.fields.size();
    }
    return total;
}

} // namespace beast::platform::dirtypersist
