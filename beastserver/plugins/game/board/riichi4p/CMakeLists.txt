include(${CMAKE_SOURCE_DIR}/plugins/cmake/BeastPlugin.cmake)

beast_add_plugin(riichi4p
    SOURCES
        plugin.cpp
        routes.cpp
        engine/riichi4p_engine.cpp
        engine/table_state.cpp
        engine/tenpai.cpp
    PROTO_DIR ${BEAST_BIZ_PROTOCOL_GAME_DIR}/board/riichi4p
    PROTOS
        riichi4p.proto
)
