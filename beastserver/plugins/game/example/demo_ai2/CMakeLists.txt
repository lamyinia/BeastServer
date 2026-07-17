beast_add_plugin(demo_ai2
    GLOB plugin.cpp routes.cpp engine/*.cpp
    PROTO_DIR ${BEAST_BIZ_PROTOCOL_GAME_DIR}/example/demo_ai2
    PROTOS demo_ai2.proto)
