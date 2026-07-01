# pixelmoba：2D 像素 MOBA 玩法插件。
#
# proto 分两套，均由框架封装生成：
#   - 通信 proto（8 张）：bizconfig/protocol/game/moba/pixel_moba/（扁平，含 types.proto）
#     走 beast_add_plugin 的 PROTOS。types.proto 原在 common/，因无其他 moba 玩法引用，
#     已并入 pixel_moba/ 以满足 BeastPlugin.cmake 的扁平布局假设。
#   - 策划表 proto（15 张）：bizconfig/scheme/moba/pixel_moba/（扁平）
#     走 beast_add_biz_protos 的 GLOB 自动发现。

beast_add_biz_protos(pixelmoba "${BEAST_BIZ_SCHEME_DIR}/moba/pixel_moba" GLOB)

# ===== 插件 =====
beast_add_plugin(pixelmoba
    SOURCES
        plugin.cpp
        engine/pixel_moba_engine.cpp
        engine/monitor_event.cpp
        engine/pathfinding.cpp
        engine/map_loader.cpp
        engine/systems/match_system.cpp
        engine/systems/movement_system.cpp
        engine/systems/combat_system.cpp
        engine/systems/economy_system.cpp
        engine/systems/map_system.cpp
    PROTO_DIR "${BEAST_BIZ_PROTOCOL_DIR}/game/moba/pixel_moba"
    PROTOS
        ping.proto
        match.proto
        lifecycle.proto
        move.proto
        combat.proto
        economy.proto
        sync.proto
        types.proto
)

target_link_libraries(beast_plugin_pixelmoba PRIVATE
    beast_plugin_pixelmoba_biz
)
