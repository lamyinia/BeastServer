#pragma once

#include "beast/mixin/dirtypersist/field_value.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace beast::platform::dirtypersist {

// 一次 flush 的最小单位：对某一行（table, id_column=id）的字段更新集合。
// 只有 dirty 字段进入 fields，未变化的字段不会落盘（这正是"自动比对字段变化"的本质）。
struct FlushOp {
    std::string table;
    std::string id;
    std::string id_column{"id"};  // 主键列名（从 EntityTraits<T>::kIdColumn 透传）
    std::vector<std::pair<std::string, FieldValue>> fields;

    [[nodiscard]] bool empty() const noexcept { return fields.empty(); }
};

// 字段级 dirty set：
// - 按 (table, id) 分组存储 dirty 字段
// - 同一字段多次 mark 用最新值覆盖（latest wins）
// - take_dirty 取出并清空（一次性 flush）
//
// 设计参考项目内 WorldState::dirty_set 思路（5 个 unordered_set<EntityId>），
// 但粒度细化到字段：用 dirty_["table:id"].fields["field_name"] = value。
class DirtyTracker {
public:
    // 标记某实体的某字段为脏（携带新值）
    // 同一字段多次 mark 用最新值覆盖
    // id_column：主键列名，由 Repository 从 EntityTraits<T>::kIdColumn 透传，
    //            backend 用它构造 WHERE 子句，默认 "id"
    void mark_field_dirty(std::string_view table, std::string_view id,
                          std::string_view field, FieldValue value,
                          std::string_view id_column = "id");

    // 标记某实体的所有字段为脏（强制全量写，用于新插入）
    void mark_entity_dirty(std::string_view table, std::string_view id,
                           std::vector<std::pair<std::string, FieldValue>> fields,
                           std::string_view id_column = "id");

    // 取出所有 dirty，按 (table, id) 分组
    // 调用后 dirty set 清空
    std::vector<FlushOp> take_dirty();

    // 强制 flush 单个实体（玩家下线/实例结束时用）
    // 返回该实体的所有 dirty 字段，并从 dirty set 移除
    std::optional<FlushOp> take_one(std::string_view table, std::string_view id);

    // 清空所有 dirty（不 flush）
    void clear() noexcept;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;  // dirty 实体数（按 table:id 计）
    [[nodiscard]] std::size_t field_count() const noexcept;  // dirty 字段总数

private:
    static std::string make_key(std::string_view table, std::string_view id) {
        std::string k;
        k.reserve(table.size() + 1 + id.size());
        k.append(table);
        k.push_back(':');
        k.append(id);
        return k;
    }

    // key = "table:id" → FlushOp（包含字段集合）
    std::unordered_map<std::string, FlushOp> dirty_;
};

} // namespace beast::platform::dirtypersist
