#include "beast/platform/rpc/grpc_server.hpp"

#include "beast/platform/core/log/logger.hpp"

#include <grpcpp/impl/service_type.h>
#include <gtest/gtest.h>

#include <memory>

namespace {

class DummyGrpcService final : public grpc::Service {};

} // namespace

TEST(GrpcServerTest, StartsAndStopsOnEphemeralPort) {
    beast::platform::core::init_log({.level = "warn"});

    beast::platform::rpc::GrpcServer server(0);
    server.register_service(std::make_shared<DummyGrpcService>());

    ASSERT_TRUE(server.start());
    EXPECT_TRUE(server.is_running());

    server.stop();
    EXPECT_FALSE(server.is_running());
}
