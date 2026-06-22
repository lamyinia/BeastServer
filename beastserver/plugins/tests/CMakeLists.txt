find_package(GTest REQUIRED)

add_executable(beast_plugin_route_handler_test route_handler_test.cpp)
target_link_libraries(beast_plugin_route_handler_test PRIVATE
    beast::engine
    beast::net
    beast::plugin_demo_event_proto
    GTest::gtest_main
)
target_compile_features(beast_plugin_route_handler_test PRIVATE cxx_std_20)
add_test(NAME beast_plugin_route_handler_test COMMAND beast_plugin_route_handler_test)
