// HotluaBroker：gRPC 线程与 engine 线程之间的请求/响应桥接器。
//
// 闭环流程：
//   gRPC 线程                          engine 线程
//   ─────────                          ───────────
//   create_request(req_id) ──┐
//                            ├── pending_[req_id] = promise
//   submit_event(req_id) ────┼──────────────────→ on_run(req_id)
//                                                   call Lua(...)
//   wait_result(fut, ...) ◀──┼─── fulfill(req_id, result)
//                            ├── pending_.erase(req_id)
//   return response          │
//
// 线程安全：mutex 保护 pending_ map。promise/future 自身线程安全。

#pragma once

#include <chrono>
#include <cstdint>
#include <future>
#include <mutex>
#include <string>
#include <unordered_map>

namespace beast::mixin::hotlua {

class HotluaBroker {
public:
    HotluaBroker() = default;
    ~HotluaBroker() = default;

    HotluaBroker(const HotluaBroker&) = delete;
    HotluaBroker& operator=(const HotluaBroker&) = delete;

    // gRPC 线程调用：注册一个 pending 请求，返回 future 供等待结果。
    // request_id 由调用方生成（如自增计数器或 IdGenerator）。
    [[nodiscard]] std::future<std::string> create_request(std::uint64_t request_id);

    // gRPC 线程调用：等待结果，超时返回 "timeout: <ms>ms"。
    // 注意：调用方负责在超时/成功后调用 cancel() 清理 pending 条目。
    [[nodiscard]] std::string wait_result(
        std::future<std::string>& fut,
        std::uint64_t request_id,
        std::chrono::milliseconds timeout);

    // engine 线程调用：完成 pending 请求，设置 promise 值。
    // 若 request_id 不在 pending_ 中（已超时或不存在），静默忽略。
    void fulfill(std::uint64_t request_id, std::string result);

    // gRPC 线程调用：清理 pending 条目（超时或出错时调用）。
    void cancel(std::uint64_t request_id);

    [[nodiscard]] std::size_t pending_count() const;

private:
    struct PendingEntry {
        std::promise<std::string> promise;
    };

    mutable std::mutex mtx_;
    std::unordered_map<std::uint64_t, PendingEntry> pending_;
};

} // namespace beast::mixin::hotlua
