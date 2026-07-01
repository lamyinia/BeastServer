# demo_tick：tick 引擎示例插件。

beast_add_biz_protos(demo_tick "${BEAST_BIZ_SCHEME_DIR}/example/npc" PROTOS npc.proto)

beast_add_plugin(demo_tick
    SOURCES
        plugin.cpp
        engine/demo_tick_engine.cpp
)

target_link_libraries(beast_plugin_demo_tick PRIVATE beast_plugin_demo_tick_biz)
