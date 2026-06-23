include(${CMAKE_SOURCE_DIR}/plugins/cmake/BeastPlugin.cmake)

beast_add_plugin(demo_ai
    SOURCES
        plugin.cpp
        routes.cpp
        engine/demo_ai_engine.cpp
    PROTOS
        demo_ai.proto
)
