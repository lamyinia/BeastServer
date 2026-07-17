find_package(GTest REQUIRED)

beast_add_gtest(plugin_route_handler
    SOURCES route_handler_test.cpp
    DEPS beast::engine beast::net beast::plugin_demo_event_proto)
