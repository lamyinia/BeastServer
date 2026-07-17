find_package(GTest REQUIRED)

beast_add_library(server
    GLOB src/*.cpp
    DEPS beast::engine beast::ai_integration beast::net beast::rpc beast::discovery beast::core)

enable_testing()

add_executable(beast_server_game_server_test tests/game_server_test.cpp)
target_link_libraries(beast_server_game_server_test PRIVATE beast_server GTest::gtest_main)
target_compile_definitions(beast_server_game_server_test PRIVATE
    BEASTSERVER_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    BEASTSERVER_BINARY_DIR="${CMAKE_BINARY_DIR}")
if(TARGET beast_plugin_demo_event)
    add_dependencies(beast_server_game_server_test beast_plugin_demo_event beast_plugin_demo_tick)
endif()
add_test(NAME beast_server_game_server_test COMMAND beast_server_game_server_test)

add_executable(beast_server_room_service_test tests/room_service_test.cpp)
target_link_libraries(beast_server_room_service_test PRIVATE beast_server GTest::gtest_main)
add_test(NAME beast_server_room_service_test COMMAND beast_server_room_service_test)
