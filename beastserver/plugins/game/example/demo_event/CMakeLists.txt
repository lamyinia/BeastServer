beast_add_plugin(demo_event
    SOURCES
        plugin.cpp
        engine/demo_event_engine.cpp
    PROTO_DIR ${BEAST_BIZ_PROTOCOL_GAME_DIR}/example/demo_event
    PROTOS
        demo_event.proto
)
