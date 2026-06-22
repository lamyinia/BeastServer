#include "beast/platform/net/session/session.hpp"
#include "beast/platform/net/session/session_manager.hpp"

#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>

namespace beast::platform::net::session {
namespace {

TEST(SessionTest, StoresInstanceId) {
    boost::asio::io_context ioc;
    Session session("player-1", Session::make_strand(ioc.get_executor()));
    EXPECT_FALSE(session.has_instance_id());
    EXPECT_TRUE(session.instance_id().empty());

    session.set_instance_id("room-42");
    EXPECT_TRUE(session.has_instance_id());
    EXPECT_EQ(session.instance_id(), "room-42");

    session.clear_instance_id();
    EXPECT_FALSE(session.has_instance_id());
}

} // namespace
} // namespace beast::platform::net::session
