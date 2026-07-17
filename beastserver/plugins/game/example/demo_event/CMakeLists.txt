beast_add_plugin(demo_event
    GLOB plugin.cpp engine/*.cpp
    PROTO_DIR ${BEAST_BIZ_PROTOCOL_GAME_DIR}/example/demo_event
    PROTOS demo_event.proto)
