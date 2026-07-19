beast_add_plugin(sdk_event
    GLOB plugin.cpp engine/*.cpp
    PROTO_DIR ${BEAST_BIZ_PROTOCOL_GAME_DIR}/example/sdk_event
    PROTOS sdk_event.proto)
