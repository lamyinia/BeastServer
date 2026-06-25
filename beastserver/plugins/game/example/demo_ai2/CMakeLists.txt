include(${CMAKE_SOURCE_DIR}/plugins/cmake/BeastPlugin.cmake)

beast_add_plugin(demo_ai2
    SOURCES
        plugin.cpp
        routes.cpp
        engine/demo_ai2_engine.cpp
    PROTO_DIR ${BEAST_BIZ_PROTOCOL_GAME_DIR}/example/demo_ai2
    PROTOS
        demo_ai2.proto
)
