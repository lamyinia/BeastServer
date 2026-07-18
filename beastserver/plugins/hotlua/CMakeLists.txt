# HotLua 集成层：LuaVm / LuaVmService / C++ 绑定
# 静态库供 platform_hotlua 插件、demo_hotlua gameplay 插件及测试链接。
find_package(sol2 CONFIG REQUIRED)
find_package(lua CONFIG REQUIRED)

beast_add_library(hotlua_integration
    GLOB src/*.cpp
    DEPS beast::core sol2::sol2 lua::lua)

# 平台 HotLua 插件 .so：注册 LuaVmService 到 ServiceRegistry。
# 输出为 build/plugins/beast_plugin_platform_hotlua.so，PluginHost Phase 1 扫描加载。
beast_add_plugin(platform_hotlua
    GLOB plugin.cpp)
target_link_libraries(beast_plugin_platform_hotlua PRIVATE beast::hotlua_integration)

# plugin_host_test 依赖此 .so 进行两阶段加载验证
if(TARGET beast_engine_plugin_host_test)
    add_dependencies(beast_engine_plugin_host_test beast_plugin_platform_hotlua)
endif()

# Lua VM 单元测试
find_package(GTest REQUIRED)
add_executable(beast_hotlua_vm_test tests/lua_vm_test.cpp)
target_link_libraries(beast_hotlua_vm_test PRIVATE
    beast::hotlua_integration GTest::gtest_main)
add_test(NAME beast_hotlua_vm_test COMMAND beast_hotlua_vm_test)
