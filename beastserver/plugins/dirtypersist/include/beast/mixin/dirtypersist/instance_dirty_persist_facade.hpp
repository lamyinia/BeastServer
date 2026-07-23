#pragma once

#include "beast/mixin/dirtypersist/dirty_persist_service.hpp"
#include "beast/mixin/dirtypersist/repository.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace beast::mixin::dirtypersist {

// InstanceDirtyPersistFacade — 玩法层入口（对应 InstanceAiFacade）
//
// 职责：
// - 持有 DirtyPersistService 裸指针（生命周期由 ServiceRegistry 持有的 shared_ptr 保证）
// - 提供 typed 接口：repository<T>() 返回 Repository<T>，玩法层操作具体实体类型
// - 提供原始接口：mark_dirty / load / flush 等，绕过 Repository 直接操作
//
// 注入路径：
// - plugin.cpp 注册 shared_ptr<DirtyPersistService> 到 "dirtypersist.service"
// - plugin.cpp 注册 shared_ptr<InstanceDirtyPersistFacade> 到 "dirtypersist.facade"
// - GameServer 查询 "dirtypersist.facade" 注入到 InstanceManager / EngineMixin
class InstanceDirtyPersistFacade {
public:
    explicit InstanceDirtyPersistFacade(platform::dirtypersist::DirtyPersistService* service)
        : service_(service) {}

    [[nodiscard]] bool available() const noexcept {
        return service_ != nullptr && service_->enabled();
    }

    [[nodiscard]] platform::dirtypersist::DirtyPersistService* service() const noexcept {
        return service_;
    }

    // ==================== 便捷接口：Repository<T> ====================

    // 获取 typed repository（每次返回新实例，内部共享 service_）
    template<platform::dirtypersist::Persistable T>
    [[nodiscard]] platform::dirtypersist::Repository<T> repository() const {
        return platform::dirtypersist::Repository<T>{service_};
    }

    // ==================== 原始接口（直接转 service_） ====================

    void mark_field_dirty(std::string_view table, std::string_view id,
                          std::string_view field, platform::dirtypersist::FieldValue value) {
        if (service_) {
            service_->mark_field_dirty(table, id, field, std::move(value));
        }
    }

    void mark_entity_dirty(std::string_view table, std::string_view id,
                           std::vector<std::pair<std::string, platform::dirtypersist::FieldValue>> fields) {
        if (service_) {
            service_->mark_entity_dirty(table, id, std::move(fields));
        }
    }

    boost::asio::awaitable<std::optional<std::unordered_map<std::string, platform::dirtypersist::FieldValue>>>
        load_one(std::string_view table, std::string_view id) {
        if (!service_) co_return std::nullopt;
        co_return co_await service_->load_one(table, id);
    }

    boost::asio::awaitable<void> flush_one(std::string_view table, std::string_view id) {
        if (service_) co_await service_->flush_one(table, id);
    }

    boost::asio::awaitable<void> flush_all() {
        if (service_) co_await service_->flush_all();
    }

    boost::asio::awaitable<void> erase_one(std::string_view table, std::string_view id) {
        if (service_) co_await service_->erase_one(table, id);
    }

    [[nodiscard]] std::size_t pending_count() const noexcept {
        return service_ ? service_->pending_count() : 0;
    }

private:
    platform::dirtypersist::DirtyPersistService* service_{nullptr};
};

} // namespace beast::mixin::dirtypersist
