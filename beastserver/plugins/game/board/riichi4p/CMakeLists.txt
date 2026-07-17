beast_add_plugin(riichi4p
    GLOB plugin.cpp routes.cpp engine/*.cpp
    PROTO_DIR ${BEAST_BIZ_PROTOCOL_GAME_DIR}/board/riichi4p
    PROTOS riichi4p.proto)
