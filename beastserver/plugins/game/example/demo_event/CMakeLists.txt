beast_add_plugin(demo_event
    SOURCES
        plugin.cpp
        engine/demo_event_engine.cpp
        routes.cpp
    PROTO_DIR ${BEAST_BIZ_PROTOCOL_GAME_DIR}/example
    PROTOS
        demo_event.proto
)
