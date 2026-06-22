#pragma once

#include <boost/asio/io_context.hpp>

#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace beast::platform::net::io {

class IoContextRunner {
public:
    explicit IoContextRunner(std::size_t thread_count = 1);

    boost::asio::io_context& context() noexcept { return ioc_; }
    [[nodiscard]] const boost::asio::io_context& context() const noexcept { return ioc_; }

    void start();
    void stop();
    [[nodiscard]] bool running() const noexcept { return running_; }

private:
    boost::asio::io_context ioc_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_;
    std::size_t thread_count_;
    std::atomic_bool running_{false};
    std::vector<std::thread> threads_;
};

} // namespace beast::platform::net::io
