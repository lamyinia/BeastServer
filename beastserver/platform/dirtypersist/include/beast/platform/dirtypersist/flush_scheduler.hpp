#pragma once

#include "beast/platform/dirtypersist/dirty_tracker.hpp"

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <functional>
#include <memory>

namespace beast::platform::dirtypersist {

// Debounce flush 调度器：
//
// 工作机制（"有脏就 flush，但不要一直抢 CPU"）：
// - notify_dirty 触发后等待 flush_delay_ms 再 flush
// - 期间所有 mark_dirty 累积到 DirtyTracker 的同一个 batch
// - timer 到期 → 触发 on_flush_ready 回调（由 DirtyPersistService 处理）
// - 无 dirty 时 timer 处于 stopped 状态，io_context 不会唤醒 → 零 CPU 占用
//
// 这是 Linux pdflush / LevelDB memtable flush / PostgreSQL bgwriter 同款模式。
class FlushScheduler {
public:
    using FlushCallback = std::function<void(std::vector<FlushOp>)>;

    FlushScheduler(boost::asio::io_context& ioc,
                   DirtyTracker& tracker,
                   std::chrono::milliseconds delay,
                   FlushCallback cb);
    ~FlushScheduler();

    FlushScheduler(const FlushScheduler&) = delete;
    FlushScheduler& operator=(const FlushScheduler&) = delete;

    // DirtyTracker.mark_dirty 后调用：
    // 若 timer 未启动 → 启动 timer
    // 若 timer 已启动 → 不做任何事（合并到同一 batch）
    void notify_dirty();

    // 强制立即 flush（玩家下线等场景）
    // 取消 pending timer、立即触发一次 flush
    void flush_now();

    // 关闭：取消所有 pending 操作，回调不再被调用
    void shutdown();

    [[nodiscard]] bool is_pending() const noexcept { return timer_running_; }

private:
    void start_timer();
    void on_timer_expire(const boost::system::error_code& ec);

    boost::asio::io_context&   ioc_;
    DirtyTracker&              tracker_;
    boost::asio::steady_timer  timer_;
    std::chrono::milliseconds  delay_;
    FlushCallback              callback_;
    bool                       timer_running_{false};
    bool                       shutting_down_{false};
};

} // namespace beast::platform::dirtypersist
