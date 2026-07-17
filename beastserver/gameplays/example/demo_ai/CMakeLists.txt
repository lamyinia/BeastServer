beast_add_plugin(demo_ai
    GLOB plugin.cpp engine/*.cpp register4ai/*.cpp)
target_link_libraries(beast_plugin_demo_ai PRIVATE beast::ai_integration)
