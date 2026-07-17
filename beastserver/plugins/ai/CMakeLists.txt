# AI 集成层：InstanceAiFacade / EngineAiHost / AiToolRegistry 等
# 从 platform/engine/ 迁移，保留 beast::platform::engine::ai 命名空间。
# 静态库供 platform_ai 插件、server 及集成测试链接。
beast_add_library(ai_integration
    GLOB src/*.cpp
    DEPS beast::engine beast::ai)

# 平台 AI 插件 .so：注册 AiService + InstanceAiFacade 到 ServiceRegistry。
# 输出为 build/plugins/beast_plugin_platform_ai.so，PluginHost Phase 1 扫描加载。
beast_add_plugin(platform_ai
    GLOB plugin.cpp)
target_link_libraries(beast_plugin_platform_ai PRIVATE beast::ai_integration)

# plugin_host_test 依赖此 .so 进行两阶段加载验证
if(TARGET beast_engine_plugin_host_test)
    add_dependencies(beast_engine_plugin_host_test beast_plugin_platform_ai)
endif()

# AI + 实例派发集成测试（从 platform/engine/tests/ 迁入，因依赖 ai_integration）
find_package(GTest REQUIRED)
add_executable(beast_ai_integration_dispatch_test tests/instance_dispatch_integration_test.cpp)
target_link_libraries(beast_ai_integration_dispatch_test PRIVATE
    beast::ai_integration beast::engine beast::net GTest::gtest_main)
add_test(NAME beast_ai_integration_dispatch_test COMMAND beast_ai_integration_dispatch_test)
