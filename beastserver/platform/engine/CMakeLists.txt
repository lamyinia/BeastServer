find_package(GTest REQUIRED)
find_package(Boost REQUIRED)

beast_add_library(engine
    GLOB src/*.cpp
    DEPS beast::core beast::bizutil beast::ai beast::net Boost::boost)

if(UNIX AND NOT APPLE)
    target_link_libraries(beast_engine PUBLIC dl)
elseif(APPLE)
    target_link_libraries(beast_engine PUBLIC ${CMAKE_DL_LIBS})
endif()

enable_testing()

add_executable(beast_engine_context_test tests/engine_context_test.cpp)
target_link_libraries(beast_engine_context_test PRIVATE beast_engine GTest::gtest_main)
add_test(NAME beast_engine_context_test COMMAND beast_engine_context_test)

add_executable(beast_engine_event_carrier_test tests/event_carrier_test.cpp)
target_link_libraries(beast_engine_event_carrier_test PRIVATE beast_engine GTest::gtest_main)
add_test(NAME beast_engine_event_carrier_test COMMAND beast_engine_event_carrier_test)

add_executable(beast_engine_loop_carrier_test tests/loop_carrier_test.cpp)
target_link_libraries(beast_engine_loop_carrier_test PRIVATE beast_engine GTest::gtest_main)
add_test(NAME beast_engine_loop_carrier_test COMMAND beast_engine_loop_carrier_test)

add_executable(beast_engine_instance_manager_test tests/instance_manager_test.cpp)
target_link_libraries(beast_engine_instance_manager_test PRIVATE beast_engine GTest::gtest_main)
add_test(NAME beast_engine_instance_manager_test COMMAND beast_engine_instance_manager_test)

add_executable(beast_engine_player_instance_registry_test tests/player_instance_registry_test.cpp)
target_link_libraries(beast_engine_player_instance_registry_test PRIVATE beast_engine GTest::gtest_main)
add_test(NAME beast_engine_player_instance_registry_test COMMAND beast_engine_player_instance_registry_test)

add_executable(beast_engine_server_context_test tests/server_context_test.cpp)
target_link_libraries(beast_engine_server_context_test PRIVATE beast_engine GTest::gtest_main)
add_test(NAME beast_engine_server_context_test COMMAND beast_engine_server_context_test)

add_executable(beast_engine_instance_dispatch_integration_test
    tests/instance_dispatch_integration_test.cpp)
target_link_libraries(beast_engine_instance_dispatch_integration_test PRIVATE beast_engine GTest::gtest_main)
add_test(NAME beast_engine_instance_dispatch_integration_test
    COMMAND beast_engine_instance_dispatch_integration_test)

add_executable(beast_engine_timer_wheel_test tests/timer_wheel_test.cpp)
target_link_libraries(beast_engine_timer_wheel_test PRIVATE beast_engine GTest::gtest_main)
add_test(NAME beast_engine_timer_wheel_test COMMAND beast_engine_timer_wheel_test)

add_executable(beast_engine_timer_service_test tests/timer_service_test.cpp)
target_link_libraries(beast_engine_timer_service_test PRIVATE beast_engine GTest::gtest_main)
add_test(NAME beast_engine_timer_service_test COMMAND beast_engine_timer_service_test)

add_executable(beast_engine_plugin_host_test tests/plugin_host_test.cpp)
target_link_libraries(beast_engine_plugin_host_test PRIVATE beast_engine GTest::gtest_main)
target_compile_definitions(beast_engine_plugin_host_test PRIVATE
    BEASTSERVER_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    BEASTSERVER_BINARY_DIR="${CMAKE_BINARY_DIR}")
if(TARGET beast_plugin_demo_event)
    add_dependencies(beast_engine_plugin_host_test beast_plugin_demo_event beast_plugin_demo_tick)
endif()
add_test(NAME beast_engine_plugin_host_test COMMAND beast_engine_plugin_host_test)
