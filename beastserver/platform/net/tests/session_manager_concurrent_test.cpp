#include "beast/platform/net/channel/channel_pipeline.hpp"
#include "beast/platform/net/session/session.hpp"
#include "beast/platform/net/session/session_manager.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <boost/asio/io_context.hpp>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace beast::platform::net::session {
namespace {

class MinimalChannel final : public channel::IChannel {
public:
    explicit MinimalChannel(std::string id) : id_(std::move(id)) {}

    [[nodiscard]] channel::ChannelType type() const noexcept override {
        return channel::ChannelType::Tcp;
    }
    [[nodiscard]] std::string id() const override { return id_; }
    [[nodiscard]] bool is_active() const override { return active_; }

    void add_inbound(std::shared_ptr<channel::ChannelInboundHandler>) override {}
    void add_outbound(std::shared_ptr<channel::ChannelOutboundHandler>) override {}
    void add_duplex(std::shared_ptr<channel::ChannelDuplexHandler>) override {}
    channel::ChannelPipeline& pipeline() override { return pipeline_; }

    void send(channel::Bytes&&) override {}
    void flush() override {}
    void close() override { active_ = false; }
    void start_read() override {}

    void transport_write(channel::Bytes&&) override {}
    void transport_flush() override {}
    void transport_close() override { active_ = false; }

    void set_on_error(OnError) override {}
    void set_on_inactive(OnInactive on_inactive) override { on_inactive_ = std::move(on_inactive); }

    void bind_session(std::shared_ptr<Session> session) override {
        session_ = std::move(session);
    }

    void dispatch(std::function<void()> fn) override {
        if (!fn) {
            return;
        }
        if (const auto session = session_.lock()) {
            session->dispatch(std::move(fn));
            return;
        }
        fn();
    }

    void fire_inactive() {
        if (on_inactive_) {
            on_inactive_();
        }
    }

private:
    channel::ChannelPipeline pipeline_{*this};
    std::string id_;
    bool active_{true};
    OnInactive on_inactive_;
    std::weak_ptr<Session> session_;
};

std::shared_ptr<SessionManager> make_manager(boost::asio::io_context& ioc) {
    return std::make_shared<SessionManager>(ioc.get_executor(), nullptr, std::chrono::seconds(5));
}

class IoThread {
public:
    explicit IoThread(boost::asio::io_context& ioc)
        : ioc_(ioc)
        , work_(boost::asio::make_work_guard(ioc))
        , thread_([&ioc]() { ioc.run(); }) {}

    ~IoThread() {
        work_.reset();
        ioc_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    boost::asio::io_context& ioc_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_;
    std::thread thread_;
};

TEST(SessionManagerConcurrentTest, ConcurrentGetSession) {
    boost::asio::io_context ioc;
    IoThread io_thread(ioc);
    const auto manager = make_manager(ioc);

    (void)manager->create_or_get_session("player-1", std::make_shared<MinimalChannel>("ch-1"));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_EQ(manager->session_count(), 1U);

    std::atomic<int> hits{0};
    std::vector<std::thread> threads;
    threads.reserve(8);
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&]() {
            for (int n = 0; n < 10'000; ++n) {
                if (manager->get_session("player-1")) {
                    hits.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(hits.load(), 80'000);
    EXPECT_EQ(manager->session_count(), 1U);
}

TEST(SessionManagerConcurrentTest, ConcurrentCreateSamePlayer) {
    boost::asio::io_context ioc;
    IoThread io_thread(ioc);
    const auto manager = make_manager(ioc);

    std::vector<std::thread> threads;
    threads.reserve(8);
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&, i]() {
            (void)manager->create_or_get_session(
                "player-dup",
                std::make_shared<MinimalChannel>("ch-" + std::to_string(i)));
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_EQ(manager->session_count(), 1U);
    const auto session = manager->get_session("player-dup");
    ASSERT_NE(session, nullptr);
}

TEST(SessionManagerConcurrentTest, ConcurrentGetWhileCreate) {
    boost::asio::io_context ioc;
    IoThread io_thread(ioc);
    const auto manager = make_manager(ioc);

    std::atomic<bool> stop{false};
    std::atomic<int> read_hits{0};

    std::thread reader([&]() {
        while (!stop.load(std::memory_order_acquire)) {
            if (manager->get_session("player-mix")) {
                read_hits.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    for (int i = 0; i < 100; ++i) {
        (void)manager->create_or_get_session(
            "player-mix",
            std::make_shared<MinimalChannel>("ch-" + std::to_string(i)));
    }

    stop.store(true, std::memory_order_release);
    reader.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_GE(read_hits.load(), 0);
    EXPECT_EQ(manager->session_count(), 1U);
}

TEST(SessionManagerConcurrentTest, BindAndLookupUnderContention) {
    boost::asio::io_context ioc;
    IoThread io_thread(ioc);
    const auto manager = make_manager(ioc);

    (void)manager->create_or_get_session("player-bind", std::make_shared<MinimalChannel>("ch-bind"));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    std::atomic<bool> stop{false};
    std::thread binder([&]() {
        for (int i = 0; i < 1'000; ++i) {
            (void)manager->bind_instance("player-bind", "room-" + std::to_string(i % 3));
            manager->unbind_instance("player-bind");
        }
        stop.store(true, std::memory_order_release);
    });

    int lookups = 0;
    while (!stop.load(std::memory_order_acquire)) {
        const auto id = manager->instance_id_for("player-bind");
        (void)id;
        ++lookups;
    }
    binder.join();

    EXPECT_GT(lookups, 0);
    EXPECT_NE(manager->get_session("player-bind"), nullptr);
}

TEST(SessionManagerConcurrentTest, RemoveSessionWhileReading) {
    boost::asio::io_context ioc;
    IoThread io_thread(ioc);
    const auto manager = make_manager(ioc);

    auto channel = std::make_shared<MinimalChannel>("ch-remove");
    (void)manager->create_or_get_session("player-remove", channel);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    std::atomic<bool> stop{false};
    std::thread reader([&]() {
        while (!stop.load(std::memory_order_acquire)) {
            (void)manager->get_session("player-remove");
        }
    });

    channel->fire_inactive();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop.store(true, std::memory_order_release);
    reader.join();

    EXPECT_EQ(manager->session_count(), 0U);
}

} // namespace
} // namespace beast::platform::net::session
