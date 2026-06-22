#include "beast/platform/net/io/io_context_runner.hpp"

namespace beast::platform::net::io {

IoContextRunner::IoContextRunner(const std::size_t thread_count)
    : work_(boost::asio::make_work_guard(ioc_))
    , thread_count_(thread_count == 0 ? 1 : thread_count) {}

void IoContextRunner::start() {
    if (running_.exchange(true)) {
        return;
    }
    threads_.reserve(thread_count_);
    for (std::size_t i = 0; i < thread_count_; ++i) {
        threads_.emplace_back([this]() { ioc_.run(); });
    }
}

void IoContextRunner::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    work_.reset();
    ioc_.stop();
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    threads_.clear();
}

} // namespace beast::platform::net::io
