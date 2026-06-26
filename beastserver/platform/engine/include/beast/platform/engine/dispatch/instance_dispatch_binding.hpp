#pragma once

#include "beast/platform/engine/carrier/i_carrier.hpp"

namespace beast::platform::engine::dispatch {

using InstanceDispatchTarget = carrier::ICarrier*;

inline void* to_dispatch_handle(InstanceDispatchTarget carrier) noexcept {
    return carrier;
}

inline InstanceDispatchTarget from_dispatch_handle(void* handle) noexcept {
    return static_cast<InstanceDispatchTarget>(handle);
}

} // namespace beast::platform::engine::dispatch
