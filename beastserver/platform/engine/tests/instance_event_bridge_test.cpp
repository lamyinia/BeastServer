#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/context/engine_context.hpp"
#include "beast/platform/engine/dispatch/instance_event_bridge.hpp"
#include "beast/platform/engine/dispatch/player_instance_registry.hpp"
#include "beast/platform/engine/instance/instance_manager.hpp"
#include "beast/platform/net/server/tcp_server.hpp"

#include <gtest/gtest.h>

#include "auth.pb.h"
#include "envelope.pb.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <vector>

namespace {

using namespace beast::platform;
namespace channel = net::channel;

channel::Bytes frame_bytes(const channel::Bytes& payload) {
    channel::Bytes framed;
    framed.reserve(4 + payload.size());
    const auto len = static_cast<std::uint32_t>(payload.size());
    framed.push_back(static_cast<std::uint8_t>((len >> 24) & 0xFF));
    framed.push_back(static_cast<std::uint8_t>((len >> 16) & 0xFF));
    framed.push_back(static_cast<std::uint8_t>((len >> 8) & 0xFF));
    framed.push_back(static_cast<std::uint8_t>(len & 0xFF));
    framed.insert(framed.end(), payload.begin(), payload.end());
    return framed;
}

channel::Bytes make_auth_login_frame(const std::string& token, const std::uint64_t client_seq = 1) {
    ::beast::net::AuthRequest auth_req;
    auth_req.set_token(token);

    ::beast::net::Envelope envelope;
    envelope.set_route("auth.login");
    envelope.set_payload(auth_req.SerializeAsString());
    envelope.set_client_seq(client_seq);

    channel::Bytes envelope_bytes(envelope.ByteSizeLong());
    envelope.SerializeToArray(envelope_bytes.data(), static_cast<int>(envelope_bytes.size()));
    return frame_bytes(envelope_bytes);
}

channel::Bytes make_route_frame(
    const std::string& route,
    const std::string& payload,
    const std::uint64_t client_seq) {
    ::beast::net::Envelope envelope;
    envelope.set_route(route);
    envelope.set_payload(payload);
    envelope.set_client_seq(client_seq);

    channel::Bytes envelope_bytes(envelope.ByteSizeLong());
    envelope.SerializeToArray(envelope_bytes.data(), static_cast<int>(envelope_bytes.size()));
    return frame_bytes(envelope_bytes);
}

bool read_framed_envelope(
    boost::asio::ip::tcp::socket& socket,
    ::beast::net::Envelope& out_envelope) {
    std::array<std::uint8_t, 4> header{};
    boost::system::error_code ec;
    boost::asio::read(socket, boost::asio::buffer(header), ec);
    if (ec) {
        return false;
    }

    std::uint32_t len = 0;
    for (int i = 0; i < 4; ++i) {
        len = (len << 8) | header[static_cast<std::size_t>(i)];
    }
    if (len == 0 || len > 1024 * 1024) {
        return false;
    }

    std::vector<std::uint8_t> body(len);
    boost::asio::read(socket, boost::asio::buffer(body), ec);
    if (ec) {
        return false;
    }

    return out_envelope.ParseFromArray(body.data(), static_cast<int>(body.size()));
}

void wait_until(const std::function<bool()>& predicate, const std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    FAIL() << "condition not met within timeout";
}

class RecordingEngine final : public engine::instance::IEngine {
public:
    void on_event(const engine::instance::InstanceEvent& event) override {
        event_count.fetch_add(1, std::memory_order_relaxed);
        last_route = event.route;
        last_payload = event.payload;
        last_player_id = event.player_id;
        last_instance_id = event.instance_id;
    }

    std::atomic<int> event_count{0};
    RouteId last_route;
    std::vector<std::uint8_t> last_payload;
    PlayerId last_player_id;
    InstanceId last_instance_id;
};

core::config::RuntimeConfig test_runtime() {
    core::config::RuntimeConfig runtime;
    runtime.event_actors.count = 1;
    runtime.event_actors.queue_capacity = 64;
    return runtime;
}

} // namespace

TEST(InstanceEventBridgeTest, ForwardsAuthorizedRouteToEngine) {
    core::init_log({.level = "warn"});

    net::server::TcpServer server({});
    engine::instance::InstanceManager instance_manager(test_runtime(), &server.outbound_hub());
    instance_manager.start();

    engine::dispatch::PlayerInstanceRegistry registry;
    engine::dispatch::InstanceEventBridge bridge(
        &server.session_manager(), &instance_manager, &registry, &server.outbound_hub());
    bridge.register_route(server.router(), "game.play");
    server.set_on_authenticated([&](const PlayerId& player_id, const std::shared_ptr<channel::IChannel>&) {
        if (const auto instance_id = registry.lookup(player_id)) {
            server.session_manager().bind_instance(player_id, *instance_id);
        }
    });
    server.router().mark_ready();

    RecordingEngine* engine_ptr = nullptr;
    ASSERT_TRUE(instance_manager.create_instance(
        "room-1",
        core::SimulationMode::EventDriven,
        {"77"},
        [&]() {
            auto engine = std::make_unique<RecordingEngine>();
            engine_ptr = engine.get();
            return engine;
        }));

    server.start();

    // gRPC 建房：玩家尚未 TCP 登录时写入逻辑路由表
    ASSERT_TRUE(registry.assign("77", "room-1"));

    boost::asio::io_context client_ioc;
    boost::asio::ip::tcp::socket client(client_ioc);
    client.connect({boost::asio::ip::make_address("127.0.0.1"), server.listen_port()});

    boost::asio::write(client, boost::asio::buffer(make_auth_login_frame("player77:secret", 1)));

    ::beast::net::Envelope auth_envelope;
    ASSERT_TRUE(read_framed_envelope(client, auth_envelope));

    EXPECT_EQ(server.session_manager().instance_id_for("77"), "room-1");

    boost::asio::write(
        client,
        boost::asio::buffer(make_route_frame("game.play", "tile-3m", 2)));

    wait_until([&]() { return engine_ptr && engine_ptr->event_count.load() == 1; }, std::chrono::seconds(2));

    EXPECT_EQ(engine_ptr->last_route, "game.play");
    EXPECT_EQ(engine_ptr->last_player_id, "77");
    EXPECT_EQ(engine_ptr->last_instance_id, "room-1");
    ASSERT_EQ(engine_ptr->last_payload.size(), 7u);
    EXPECT_EQ(
        std::string(engine_ptr->last_payload.begin(), engine_ptr->last_payload.end()),
        "tile-3m");

    client.close();
    instance_manager.stop();
    server.stop();
}

TEST(InstanceEventBridgeTest, RejectsPlayerWithoutInstanceBinding) {
    core::init_log({.level = "warn"});

    net::server::TcpServer server({});
    engine::instance::InstanceManager instance_manager(test_runtime(), &server.outbound_hub());
    instance_manager.start();

    engine::dispatch::PlayerInstanceRegistry registry;
    engine::dispatch::InstanceEventBridge bridge(
        &server.session_manager(), &instance_manager, &registry, &server.outbound_hub());
    bridge.register_route(server.router(), "game.play");
    server.router().mark_ready();
    server.start();

    boost::asio::io_context client_ioc;
    boost::asio::ip::tcp::socket client(client_ioc);
    client.connect({boost::asio::ip::make_address("127.0.0.1"), server.listen_port()});

    boost::asio::write(client, boost::asio::buffer(make_auth_login_frame("player88:secret", 1)));
    ::beast::net::Envelope auth_envelope;
    ASSERT_TRUE(read_framed_envelope(client, auth_envelope));

    boost::asio::write(
        client,
        boost::asio::buffer(make_route_frame("game.play", "tile-1m", 2)));

    ::beast::net::Envelope error_envelope;
    ASSERT_TRUE(read_framed_envelope(client, error_envelope));
    EXPECT_EQ(error_envelope.route(), "game.play.response");
    EXPECT_NE(error_envelope.payload().find("not in instance"), std::string::npos);

    client.close();
    instance_manager.stop();
    server.stop();
}

TEST(InstanceEventBridgeTest, UnbindsPlayersWhenInstanceEnds) {
    core::init_log({.level = "warn"});

    net::server::TcpServer server({});
    engine::instance::InstanceManager instance_manager(test_runtime(), &server.outbound_hub());
    instance_manager.start();

    engine::dispatch::PlayerInstanceRegistry registry;
    engine::dispatch::InstanceEventBridge bridge(
        &server.session_manager(), &instance_manager, &registry, &server.outbound_hub());
    bridge.attach_instance_lifecycle();
    bridge.register_route(server.router(), "game.finish");
    server.router().mark_ready();

    class EndingEngine final : public engine::instance::IEngine {
    public:
        void on_start(engine::context::EngineContext& ctx) override { ctx_ = &ctx; }

        void on_event(const engine::instance::InstanceEvent& /*event*/) override {
            if (ctx_) {
                ctx_->notify_instance_end();
            }
        }

        engine::context::EngineContext* ctx_{nullptr};
    };

    ASSERT_TRUE(instance_manager.create_instance(
        "room-end",
        core::SimulationMode::EventDriven,
        {"55"},
        []() { return std::make_unique<EndingEngine>(); }));

    server.start();

    boost::asio::io_context client_ioc;
    boost::asio::ip::tcp::socket client(client_ioc);
    client.connect({boost::asio::ip::make_address("127.0.0.1"), server.listen_port()});

    boost::asio::write(client, boost::asio::buffer(make_auth_login_frame("player55:secret", 1)));
    ::beast::net::Envelope auth_envelope;
    ASSERT_TRUE(read_framed_envelope(client, auth_envelope));

    ASSERT_TRUE(bridge.bind_player("55", "room-end"));

    boost::asio::write(
        client,
        boost::asio::buffer(make_route_frame("game.finish", "", 2)));

    wait_until(
        [&]() { return server.session_manager().instance_id_for("55").empty(); },
        std::chrono::seconds(2));
    EXPECT_FALSE(instance_manager.has_instance("room-end"));

    client.close();
    instance_manager.stop();
    server.stop();
}
