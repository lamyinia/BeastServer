#pragma once

#include "beast/platform/core/types.hpp"
#include "beast/platform/engine/context/engine_context.hpp"
#include "beast/platform/engine/instance/i_engine.hpp"
#include "beast/platform/engine/instance/instance_event.hpp"

namespace beast::platform::engine::capability {

namespace detail {

template <class Derived, template <class> class... Mixins>
consteval int max_start_order() {
    int order = 0;
    ((order = Mixins<Derived>::kStartOrder > order ? Mixins<Derived>::kStartOrder : order), ...);
    return order;
}

template <class Derived, template <class> class MixinA, template <class> class MixinB>
consteval bool starts_before() {
    return MixinA<Derived>::kStartOrder <= MixinB<Derived>::kStartOrder;
}

template <class Derived, template <class> class... Mixins>
void invoke_start_hooks(Derived& self, context::EngineContext& ctx) {
    (Mixins<Derived>::on_start_hook(self, ctx), ...);
}

template <class Derived, template <class> class... Mixins>
bool invoke_event_hooks(Derived& self, const instance::InstanceEvent& event) {
    return (Mixins<Derived>::on_event_hook(self, event) || ...);
}

template <class Derived, template <class> class Mixin>
void invoke_tick_hook_if_present(
    Derived& self,
    const beast::platform::Tick tick,
    const beast::platform::TimestampMs dt_ms) {
    if constexpr (requires { Mixin<Derived>::on_tick_hook(self, tick, dt_ms); }) {
        Mixin<Derived>::on_tick_hook(self, tick, dt_ms);
    }
}

template <class Derived, template <class> class... Mixins>
void invoke_tick_hooks(
    Derived& self,
    const beast::platform::Tick tick,
    const beast::platform::TimestampMs dt_ms) {
    (invoke_tick_hook_if_present<Derived, Mixins>(self, tick, dt_ms), ...);
}

} // namespace detail

// 聚合 EngineRoot：唯一 override on_start/on_event/on_tick，按 Mixin hook 组合能力插片。
template <class Derived, template <class> class... Mixins>
class EngineRoot : public instance::IEngine {
public:
    void on_start(context::EngineContext& ctx) final {
        detail::invoke_start_hooks<Derived, Mixins...>(derived(), ctx);
        derived().on_engine_start(ctx);
    }

    void on_event(const instance::InstanceEvent& event) final {
        if (detail::invoke_event_hooks<Derived, Mixins...>(derived(), event)) {
            return;
        }
        derived().on_game_event(event);
    }

    void on_tick(const beast::platform::Tick tick, const beast::platform::TimestampMs dt_ms) final {
        detail::invoke_tick_hooks<Derived, Mixins...>(derived(), tick, dt_ms);
        if constexpr (requires { derived().on_engine_tick(tick, dt_ms); }) {
            derived().on_engine_tick(tick, dt_ms);
        }
    }

protected:
    [[nodiscard]] Derived& derived() { return static_cast<Derived&>(*this); }
    [[nodiscard]] const Derived& derived() const { return static_cast<const Derived&>(*this); }
};

} // namespace beast::platform::engine::capability
