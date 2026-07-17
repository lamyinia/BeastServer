beast_add_plugin(demo_tick
    GLOB plugin.cpp engine/*.cpp
    BIZ_SCHEME "${BEAST_BIZ_SCHEME_DIR}/example/npc"
    BIZ_PROTOS npc.proto)
