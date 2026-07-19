# DirtyPersist 集成层：InstanceDirtyPersistFacade（玩法层入口）
# 命名空间 beast::mixin::dirtypersist
#
# Facade 当前为 header-only inline（无 .cpp），用 INTERFACE library 暴露 include/。
# 未来若新增集成逻辑（lifecycle hook、proto event 等），可改为常规 static library：
#   beast_add_library(dirtypersist_integration GLOB src/*.cpp DEPS beast::engine beast::dirtypersist)
add_library(dirtypersist_integration INTERFACE)
add_library(beast::dirtypersist_integration ALIAS dirtypersist_integration)
target_include_directories(dirtypersist_integration INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(dirtypersist_integration INTERFACE beast::dirtypersist)

# 平台 Dirtypersist 插件 .so：注册 DirtyPersistService + InstanceDirtyPersistFacade 到 ServiceRegistry
# 输出为 build/plugins/beast_plugin_platform_dirtypersist.so，PluginHost Phase 1 扫描加载
beast_add_plugin(platform_dirtypersist
    GLOB plugin.cpp)
target_link_libraries(beast_plugin_platform_dirtypersist PRIVATE beast::dirtypersist_integration)

# plugin_host_test 依赖此 .so 进行两阶段加载验证
if(TARGET beast_engine_plugin_host_test)
    add_dependencies(beast_engine_plugin_host_test beast_plugin_platform_dirtypersist)
endif()
