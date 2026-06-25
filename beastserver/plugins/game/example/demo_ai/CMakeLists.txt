include(${CMAKE_SOURCE_DIR}/plugins/cmake/BeastPlugin.cmake)

beast_add_plugin(demo_ai
    SOURCES
        plugin.cpp
        register4ai/receipts.cpp
        engine/demo_ai_engine.cpp
)
