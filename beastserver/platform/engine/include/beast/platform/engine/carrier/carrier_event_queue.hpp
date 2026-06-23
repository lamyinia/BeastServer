#pragma once

#include "beast/platform/engine/instance/instance_event.hpp"

#include <boost/lockfree/queue.hpp>

#include <atomic>
#include <cstddef>

namespace beast::platform::engine::carrier {

inline constexpr std::size_t kDefaultInstanceEventQueueCapacity = 8192;

// IO 线程 MPSC 投递；boost::lockfree::queue 要求 trivial 元素，事件体堆分配。
using InstanceEventNode = instance::InstanceEvent*;

using InstanceEventIngressQueue = boost::lockfree::queue<
    InstanceEventNode,
    boost::lockfree::fixed_sized<true>,
    boost::lockfree::capacity<kDefaultInstanceEventQueueCapacity>>;

class InstanceEventIngress {
public:
    [[nodiscard]] bool push(const instance::InstanceEvent& event) {
        auto* node = new instance::InstanceEvent(event);
        if (queue_.push(node)) {
            pending_.fetch_add(1, std::memory_order_release);
            return true;
        }
        delete node;
        return false;
    }

    [[nodiscard]] bool pop(instance::InstanceEvent& out) {
        InstanceEventNode node = nullptr;
        if (!queue_.pop(node) || node == nullptr) {
            return false;
        }
        out = std::move(*node);
        delete node;
        pending_.fetch_sub(1, std::memory_order_release);
        return true;
    }

    void drain() {
        instance::InstanceEvent discarded;
        while (pop(discarded)) {
        }
    }

    [[nodiscard]] std::size_t pending() const noexcept {
        return pending_.load(std::memory_order_acquire);
    }

private:
    InstanceEventIngressQueue queue_;
    std::atomic<std::size_t> pending_{0};
};

} // namespace beast::platform::engine::carrier
