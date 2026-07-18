// HotluaBroker 实现：mutex 保护的 promise/future 桥接。

#include "beast/mixin/hotlua/hotlua_broker.hpp"

namespace beast::mixin::hotlua {

std::future<std::string> HotluaBroker::create_request(std::uint64_t request_id) {
    std::promise<std::string> promise;
    auto future = promise.get_future();
    std::lock_guard lock(mtx_);
    // 若已存在（重复 request_id），覆盖旧 promise（旧 future 将 broken_promise）
    pending_[request_id] = PendingEntry{std::move(promise)};
    return future;
}

std::string HotluaBroker::wait_result(
    std::future<std::string>& fut,
    std::uint64_t /*request_id*/,
    std::chrono::milliseconds timeout) {
    if (fut.wait_for(timeout) != std::future_status::ready) {
        return "timeout";
    }
    try {
        return fut.get();
    } catch (const std::exception& e) {
        return std::string("error: ") + e.what();
    }
}

void HotluaBroker::fulfill(std::uint64_t request_id, std::string result) {
    std::promise<std::string> promise;
    {
        std::lock_guard lock(mtx_);
        auto it = pending_.find(request_id);
        if (it == pending_.end()) {
            // 已超时或不存在，静默忽略
            return;
        }
        promise = std::move(it->second.promise);
        pending_.erase(it);
    }
    promise.set_value(std::move(result));
}

void HotluaBroker::cancel(std::uint64_t request_id) {
    std::lock_guard lock(mtx_);
    pending_.erase(request_id);
}

std::size_t HotluaBroker::pending_count() const {
    std::lock_guard lock(mtx_);
    return pending_.size();
}

} // namespace beast::mixin::hotlua
