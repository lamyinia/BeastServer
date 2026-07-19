#pragma once

#include "beast/platform/dirtypersist/dirty_persist_service.hpp"
#include "beast/platform/dirtypersist/entity_traits.hpp"
#include "beast/platform/dirtypersist/field_value.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace beast::platform::dirtypersist {

// Repository<T>：对特定实体类型 T 的类型化访问门面
//
// 设计要点：
// - load 时从 DB 加载 + 缓存 identity_map（避免同一 id 重复查询）
// - mark_dirty 时自动 diff 旧值（snapshots_），仅提交变化字段
//   → 用户不需要手动写"哪些字段变了"，这是"自动比对字段变化"的实现
// - 提供 typed 接口，避免用户手写 string field name
template<Persistable T>
class Repository {
public:
    using entity_type = T;

    explicit Repository(DirtyPersistService* service) : service_(service) {}

    // 加载实体（首次访问从 DB 查，缓存到 identity_map）
    // 后续同 id 调用直接返回缓存（identity map 模式）
    boost::asio::awaitable<std::shared_ptr<T>> load(std::string id) {
        if (auto it = identity_map_.find(id); it != identity_map_.end()) {
            if (auto sp = it->second.lock()) {
                co_return sp;
            }
            identity_map_.erase(it);
        }

        // 从 DB 加载字段，传入主键列名让 backend 构造 WHERE 子句
        auto fields_opt = co_await service_->load_one(
            EntityTraits<T>::kTable, id, EntityTraits<T>::kIdColumn);
        if (!fields_opt) {
            co_return nullptr;
        }

        auto entity = std::make_shared<T>();
        for (const auto& [name, value] : *fields_opt) {
            const std::size_t idx = find_field_idx<T>(name);
            if (idx != static_cast<std::size_t>(-1)) {
                set_field(*entity, idx, value);
            }
        }

        // 缓存 + 快照（用于后续 diff）
        identity_map_[id] = entity;
        snapshots_[id]    = *entity;
        co_return entity;
    }

    // 标记整个实体 dirty（自动 diff 旧值，仅提交变化字段）
    // 没有 snapshot 的实体（首次创建）会全字段提交
    void mark_dirty(const std::shared_ptr<T>& entity) {
        if (!entity) return;

        const std::string id = get_id(*entity);
        std::vector<std::pair<std::string, FieldValue>> dirty_fields;

        auto snap_it = snapshots_.find(id);
        if (snap_it == snapshots_.end()) {
            // 首次创建 → 全字段提交
            dirty_fields = flatten(*entity);
        } else {
            // diff 旧值，仅收集变化字段
            std::vector<std::pair<std::size_t, FieldValue>> diff_idx;
            diff(snap_it->second, *entity, diff_idx);
            const auto& meta = EntityTraits<T>::kFields;
            dirty_fields.reserve(diff_idx.size());
            for (const auto& [idx, value] : diff_idx) {
                dirty_fields.emplace_back(std::string{meta[idx].name}, value);
            }
            // 更新 snapshot
            snap_it->second = *entity;
        }

        if (dirty_fields.empty()) return;
        // 透传 EntityTraits<T>::kIdColumn 到 backend
        service_->mark_entity_dirty(EntityTraits<T>::kTable, id,
                                     std::move(dirty_fields),
                                     EntityTraits<T>::kIdColumn);
    }

    // 标记单字段 dirty（直接调 service，不经过 diff）
    // 用于已知字段变更的高频路径（如 hp 每次扣血）
    template<typename U>
    void mark_field_dirty(const std::shared_ptr<T>& entity,
                          std::string_view field_name, U&& value) {
        if (!entity) return;
        const std::string id = get_id(*entity);
        service_->mark_field_dirty(
            EntityTraits<T>::kTable, id, field_name,
            FieldValue{std::forward<U>(value)},
            EntityTraits<T>::kIdColumn);
    }

    // 强制 flush 单个实体（玩家下线）
    boost::asio::awaitable<void> flush_one(const std::shared_ptr<T>& entity) {
        if (!entity) co_return;
        const std::string id = get_id(*entity);
        // 先 mark_dirty 确保最新值进入 tracker
        mark_dirty(entity);
        co_await service_->flush_one(EntityTraits<T>::kTable, id);
    }

    // 删除一行（玩家注销等场景）
    boost::asio::awaitable<void> erase_one(const std::shared_ptr<T>& entity) {
        if (!entity) co_return;
        const std::string id = get_id(*entity);
        co_await service_->erase_one(EntityTraits<T>::kTable, id,
                                      EntityTraits<T>::kIdColumn);
        // 从 identity map 移除缓存
        evict(id);
    }

    // 从 identity map 移除（玩家下线后释放缓存）
    void evict(const std::string& id) {
        identity_map_.erase(id);
        snapshots_.erase(id);
    }

    [[nodiscard]] DirtyPersistService* service() const noexcept { return service_; }

private:
    DirtyPersistService* service_;
    std::unordered_map<std::string, std::weak_ptr<T>> identity_map_;
    std::unordered_map<std::string, T>                snapshots_;  // 用于 diff 旧值
};

} // namespace beast::platform::dirtypersist
