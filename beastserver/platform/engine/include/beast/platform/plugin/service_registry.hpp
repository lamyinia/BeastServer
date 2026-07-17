#pragma once

#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>

namespace beast::platform::plugin {

// 平台级服务注册表：platform plugin 在 beast_platform_plugin_init 中
// 通过 PlatformContext::register_service<T> 注册共享服务实例，
// GameServer / 其他 plugin 通过 get_service<T> 按名查询。
//
// 使用 shared_ptr<void> + type_index 做类型擦除与类型安全校验：
// - 注册时记录 typeid(T)，查询时校验 typeid 匹配，避免类型不匹配的 reinterpret_cast。
// - 服务生命周期由 ServiceRegistry 持有，registry 销毁时所有服务自动析构。
class ServiceRegistry {
public:
    template<typename T>
    void register_service(std::string name, std::shared_ptr<T> service) {
        if (name.empty() || !service) {
            return;
        }
        services_[std::move(name)] = Entry{
            std::static_pointer_cast<void>(service),
            std::type_index(typeid(T)),
        };
    }

    template<typename T>
    [[nodiscard]] std::shared_ptr<T> get_service(const std::string& name) const {
        const auto it = services_.find(name);
        if (it == services_.end()) {
            return nullptr;
        }
        if (it->second.type != std::type_index(typeid(T))) {
            return nullptr;
        }
        return std::static_pointer_cast<T>(it->second.ptr);
    }

    [[nodiscard]] bool has_service(const std::string& name) const noexcept {
        return services_.contains(name);
    }

    [[nodiscard]] std::size_t size() const noexcept { return services_.size(); }

private:
    struct Entry {
        std::shared_ptr<void> ptr;
        std::type_index type{typeid(void)};
    };

    std::unordered_map<std::string, Entry> services_;
};

} // namespace beast::platform::plugin
