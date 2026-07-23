#include "beast/mixin/dirtypersist/flush_scheduler.hpp"

#include "beast/platform/core/log/logger.hpp"

namespace beast::platform::dirtypersist {

FlushScheduler::FlushScheduler(boost::asio::io_context& ioc,
                               DirtyTracker& tracker,
                               std::chrono::milliseconds delay,
                               FlushCallback cb)
    : ioc_(ioc)
    , tracker_(tracker)
    , timer_(ioc)
    , delay_(delay)
    , callback_(std::move(cb)) {}

FlushScheduler::~FlushScheduler() {
    shutdown();
}

void FlushScheduler::notify_dirty() {
    if (shutting_down_) return;
    if (timer_running_) {
        // Timer 已经在跑 → 累积到同一 batch，不重新计时
        // 这样高频 mark_dirty 不会无限延期 flush
        return;
    }
    start_timer();
}

void FlushScheduler::start_timer() {
    if (delay_.count() == 0) {
        // flush_delay_ms = 0 → 立即触发（用于测试或对延迟敏感的场景）
        on_timer_expire({});
        return;
    }
    timer_running_ = true;
    timer_.expires_after(delay_);
    timer_.async_wait([this](const boost::system::error_code& ec) {
        on_timer_expire(ec);
    });
}

void FlushScheduler::on_timer_expire(const boost::system::error_code& ec) {
    timer_running_ = false;
    if (shutting_down_) return;
    if (ec == boost::asio::error::operation_aborted) {
        // 被取消（flush_now 或 shutdown 触发），不回调
        return;
    }
    if (ec) {
        BEAST_LOG_WARN("dirtypersist: flush timer error: {}", ec.message());
        return;
    }
    if (tracker_.empty()) return;

    auto batch = tracker_.take_dirty();
    if (batch.empty()) return;

    if (callback_) {
        callback_(std::move(batch));
    }

    // 处理完之后若又有新 dirty（callback 期间触发的），重新启动 timer
    if (!tracker_.empty()) {
        start_timer();
    }
}

void FlushScheduler::flush_now() {
    if (shutting_down_) return;
    if (timer_running_) {
        boost::system::error_code ec;
        timer_.cancel(ec);
        timer_running_ = false;
    }
    if (tracker_.empty()) return;

    auto batch = tracker_.take_dirty();
    if (batch.empty()) return;
    if (callback_) {
        callback_(std::move(batch));
    }
}

void FlushScheduler::shutdown() {
    if (shutting_down_) return;
    shutting_down_ = true;
    boost::system::error_code ec;
    timer_.cancel(ec);
    timer_running_ = false;
}

} // namespace beast::platform::dirtypersist
