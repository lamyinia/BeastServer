beast_add_plugin(demo_hotlua
    GLOB plugin.cpp engine/*.cpp
    PROTO_DIR ${BEAST_BIZ_PROTOCOL_GAME_DIR}/example/demo_hotlua
    PROTOS demo_hotlua.proto)
target_link_libraries(beast_plugin_demo_hotlua PRIVATE beast::hotlua_integration)
