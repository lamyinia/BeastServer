beast_add_plugin(stress_event
    GLOB plugin.cpp engine/*.cpp algo/*.cpp
    PROTO_DIR ${BEAST_BIZ_PROTOCOL_GAME_DIR}/example/stress
    PROTOS stress.proto)
