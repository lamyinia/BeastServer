#include "beast/platform/net/channel/channel_pipeline.hpp"
#include "beast/platform/net/channel/i_channel.hpp"
#include "beast/platform/net/channel/i_channel_handler.hpp"
#include "beast/platform/net/dispatch/router.hpp"
#include "beast/platform/net/outbound/outbound_hub.hpp"
#include "beast/platform/net/outbound/outbound_send_result.hpp"
#include "beast/platform/net/session/session.hpp"
#include "beast/platform/net/session/session_manager.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace {

using namespace beast::platform::net;

class CaptureOutboundHandler final : public channel::ChannelOutboundHandler {
public:
    void write(channel::ChannelHandlerContext& ctx, channel::OutboundMessage&& msg) override {
        (void)ctx;
        if (std::holds_alternative<channel::MessagePtr>(msg)) {
            const auto& message = std::get<channel::MessagePtr>(msg);
            last_route_ = message->route;
            ++write_count_;
        }
    }

    std::string last_route_;
    std::atomic<int> write_count_{0};
};

class FakeChannel final : public channel::IChannel {
public:
    FakeChannel(channel::ChannelType type, std::shared_ptr<CaptureOutboundHandler> handler)
        : type_(type)
        , pipeline_(*this)
        , handler_(std::move(handler)) {
        pipeline_.add_outbound(handler_);
    }

    [[nodiscard]] channel::ChannelType type() const noexcept override { return type_; }
    [[nodiscard]] std::string id() const override { return id_; }
    [[nodiscard]] bool is_active() const override { return active_; }

    void add_inbound(std::shared_ptr<channel::ChannelInboundHandler> handler) override {
        pipeline_.add_inbound(std::move(handler));
    }
    void add_outbound(std::shared_ptr<channel::ChannelOutboundHandler> handler) override {
        pipeline_.add_outbound(std::move(handler));
    }
    void add_duplex(std::shared_ptr<channel::ChannelDuplexHandler> handler) override {
        pipeline_.add_duplex(std::move(handler));
    }
    channel::ChannelPipeline& pipeline() override { return pipeline_; }

    void send(channel::Bytes&& data) override { pipeline_.fire_write(std::move(data)); }
    void flush() override { pipeline_.fire_flush(); }
    void close() override { active_ = false; }
    void start_read() override {}

    void transport_write(channel::Bytes&&) override {}
    void transport_flush() override {}
    void transport_close() override { active_ = false; }

    void set_on_error(OnError) override {}
    void set_on_inactive(OnInactive on_inactive) override { on_inactive_ = std::move(on_inactive); }

    void bind_session(std::shared_ptr<session::Session> session) override {
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

    std::shared_ptr<CaptureOutboundHandler> handler() const { return handler_; }

private:
    channel::ChannelType type_;
    channel::ChannelPipeline pipeline_;
    std::shared_ptr<CaptureOutboundHandler> handler_;
    std::string id_{"fake-outbound-1"};
    bool active_{true};
    OnInactive on_inactive_;
    std::weak_ptr<session::Session> session_;
};

} // namespace

TEST(OutboundHubTest, SendFromWorkerThreadDispatchesOnIoThread) {
    boost::asio::io_context ioc;
    auto work = boost::asio::make_work_guard(ioc);
    const auto router = std::make_shared<dispatch::Router>();
    const auto session_manager = std::make_shared<session::SessionManager>(
        ioc.get_executor(),
        router,
        std::chrono::seconds(5));

    auto capture = std::make_shared<CaptureOutboundHandler>();
    auto channel = std::make_shared<FakeChannel>(channel::ChannelType::Tcp, capture);
    (void)session_manager->create_or_get_session("player-1", channel);

    outbound::OutboundHub hub(ioc, session_manager);

    std::thread io_thread([&]() { ioc.run(); });

    auto msg = std::make_shared<channel::Message>();
    msg->route = "game.ping";
    hub.send("player-1", std::move(msg));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    work.reset();
    ioc.stop();
    io_thread.join();

    EXPECT_EQ(capture->write_count_.load(), 1);
    EXPECT_EQ(capture->last_route_, "game.ping");
}

TEST(OutboundHubTest, PendingConnectionPromotedAfterAuth) {
    boost::asio::io_context ioc;
    const auto router = std::make_shared<dispatch::Router>();
    auto session_manager = std::make_shared<session::SessionManager>(
        ioc.get_executor(),
        router,
        std::chrono::seconds(5));

    auto capture = std::make_shared<CaptureOutboundHandler>();
    auto channel = std::make_shared<FakeChannel>(channel::ChannelType::Tcp, capture);

    session_manager->on_new_connection(channel);
    EXPECT_EQ(session_manager->pending_count(), 1U);
    EXPECT_EQ(session_manager->session_count(), 0U);

    session_manager->on_auth_success(channel->id(), "player-42");
    ioc.run();

    EXPECT_EQ(session_manager->pending_count(), 0U);
    EXPECT_EQ(session_manager->session_count(), 1U);
    EXPECT_NE(session_manager->get_session("player-42"), nullptr);
}

TEST(OutboundHubTest, BroadcastDeliversToAllPlayers) {
    boost::asio::io_context ioc;
    auto work = boost::asio::make_work_guard(ioc);
    const auto router = std::make_shared<dispatch::Router>();
    const auto session_manager = std::make_shared<session::SessionManager>(
        ioc.get_executor(),
        router,
        std::chrono::seconds(5));

    auto capture_a = std::make_shared<CaptureOutboundHandler>();
    auto capture_b = std::make_shared<CaptureOutboundHandler>();
    (void)session_manager->create_or_get_session(
        "player-a",
        std::make_shared<FakeChannel>(channel::ChannelType::Tcp, capture_a));
    (void)session_manager->create_or_get_session(
        "player-b",
        std::make_shared<FakeChannel>(channel::ChannelType::Tcp, capture_b));

    outbound::OutboundHub hub(ioc, session_manager);
    std::thread io_thread([&]() { ioc.run(); });

    auto msg = std::make_shared<channel::Message>();
    msg->route = "game.state";
    hub.broadcast({"player-a", "player-b"}, msg);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    work.reset();
    ioc.stop();
    io_thread.join();

    EXPECT_EQ(capture_a->write_count_.load(), 1);
    EXPECT_EQ(capture_b->write_count_.load(), 1);
    EXPECT_EQ(capture_a->last_route_, "game.state");
    EXPECT_EQ(capture_b->last_route_, "game.state");
}

TEST(OutboundHubTest, PreferTcpDoesNotFallbackToOtherProtocol) {
    boost::asio::io_context ioc;
    auto work = boost::asio::make_work_guard(ioc);
    const auto router = std::make_shared<dispatch::Router>();
    const auto session_manager = std::make_shared<session::SessionManager>(
        ioc.get_executor(),
        router,
        std::chrono::seconds(5));

    auto capture = std::make_shared<CaptureOutboundHandler>();
    (void)session_manager->create_or_get_session(
        "player-ws",
        std::make_shared<FakeChannel>(channel::ChannelType::Websocket, capture));

    outbound::OutboundHub hub(ioc, session_manager);
    std::thread io_thread([&]() { ioc.run(); });

    std::atomic<outbound::OutboundSendResult> result{outbound::OutboundSendResult::Ok};
    auto msg = std::make_shared<channel::Message>();
    msg->route = "game.ping";
    hub.send(
        "player-ws",
        std::move(msg),
        outbound::ProtocolPreference::PreferTcp,
        [&](const outbound::OutboundSendResult r) { result.store(r); });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    work.reset();
    ioc.stop();
    io_thread.join();

    EXPECT_EQ(capture->write_count_.load(), 0);
    EXPECT_EQ(result.load(), outbound::OutboundSendResult::NoChannel);
}

TEST(OutboundHubTest, SendReportsSessionNotRegisteredAfterRemoval) {
    boost::asio::io_context ioc;
    auto work = boost::asio::make_work_guard(ioc);
    const auto router = std::make_shared<dispatch::Router>();
    const auto session_manager = std::make_shared<session::SessionManager>(
        ioc.get_executor(),
        router,
        std::chrono::seconds(5));

    auto capture = std::make_shared<CaptureOutboundHandler>();
    auto channel = std::make_shared<FakeChannel>(channel::ChannelType::Tcp, capture);
    (void)session_manager->create_or_get_session("player-gone", channel);

    outbound::OutboundHub hub(ioc, session_manager);
    std::thread io_thread([&]() { ioc.run(); });

    const auto session = session_manager->get_session("player-gone");
    ASSERT_NE(session, nullptr);

    // 先在 strand 上排队 remove，再 send（send 同步 get_session 仍成功，strand 任务后执行）。
    session->dispatch([&]() { session_manager->remove_session("player-gone"); });

    std::atomic<outbound::OutboundSendResult> result{outbound::OutboundSendResult::Ok};
    auto msg = std::make_shared<channel::Message>();
    msg->route = "game.ping";
    hub.send(
        "player-gone",
        std::move(msg),
        outbound::ProtocolPreference::PreferTcp,
        [&](const outbound::OutboundSendResult r) { result.store(r); });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    work.reset();
    ioc.stop();
    io_thread.join();

    EXPECT_EQ(result.load(), outbound::OutboundSendResult::SessionNotRegistered);
    EXPECT_EQ(capture->write_count_.load(), 0);
}
