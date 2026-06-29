find_package(GTest REQUIRED)

add_library(beast_server STATIC
    src/game_server.cpp
    src/room_service.cpp
    src/room_service_grpc.cpp
)

add_library(beast::server ALIAS beast_server)

target_include_directories(beast_server
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(beast_server
    PUBLIC
        beast::engine
        beast::net
        beast::rpc
        beast::core
)

target_compile_features(beast_server PUBLIC cxx_std_20)

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

# 验收点 9：grpcurl CreateRoom(demo_event, 42, test-room-001) 后 KCP 客户端发 ping1 收 pong1。
# 注：本 target 依赖 beast_plugin_demo_event_proto，而 plugin target 在根 CMakeLists.txt
# 的 add_subdirectory(plugins) 之后才创建，故本 target 定义挪到根 CMakeLists.txt 末尾
# （plugins 之后）以保证 if(TARGET ...) 为真。
