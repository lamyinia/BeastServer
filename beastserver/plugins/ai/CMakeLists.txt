find_package(CURL REQUIRED)
find_package(Boost REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(GTest REQUIRED)

# ── beast::ai：AI 服务核心库（AiService / HttpClient / Drivers / Protobuf 事件） ──
# 命名空间 beast::platform::ai（保留，不随目录迁移改变）。
set(_ai_proto_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
beast_generate_protobuf(
    OUT_DIR "${_ai_proto_dir}"
    PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto"
    PROTOS ai_event.proto
    OUT_SOURCES _ai_proto_src
    COMMENT "Generating plugins/ai protobuf sources")

beast_add_library(ai
    SOURCES
        src/ai_config.cpp
        src/service/ai_service.cpp
        src/client/http_client.cpp
        src/client/sse_parser.cpp
        src/client/volc_signer.cpp
        src/driver/ai_driver.cpp
        src/driver/driver_util.cpp
        src/driver/openai_driver.cpp
        src/driver/volcengine_driver.cpp
        src/model/ai_types.cpp
        ${_ai_proto_src}
    INCLUDE_DIRS "${_ai_proto_dir}"
    DEPS beast::core Boost::boost CURL::libcurl OpenSSL::Crypto protobuf::libprotobuf)

# ── beast::ai_integration：AI 集成层（InstanceAiFacade / EngineAiHost / AiToolRegistry 等） ──
# 命名空间 beast::mixin::ai（plugins/ 下 mixin 类插件统一约定）。
# 静态库供 platform_ai 插件、server 及集成测试链接。
beast_add_library(ai_integration
    SOURCES
        src/engine_ai_host.cpp
        src/engine_ai_decisions.cpp
        src/engine_ai_declarative.cpp
        src/engine_ai_events.cpp
        src/instance_ai_facade.cpp
        src/ai_tool_dispatch.cpp
        src/ai_event_dispatch.cpp
        src/ai_tool_registry.cpp
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

# ── 测试 ──
enable_testing()

add_executable(beast_ai_service_test tests/ai_service_test.cpp)
target_compile_definitions(beast_ai_service_test PRIVATE BEASTSERVER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(beast_ai_service_test PRIVATE beast_ai beast::core GTest::gtest_main)
add_test(NAME beast_ai_service_test COMMAND beast_ai_service_test)

add_executable(beast_ai_embed_test tests/ai_embed_test.cpp)
target_compile_definitions(beast_ai_embed_test PRIVATE BEASTSERVER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(beast_ai_embed_test PRIVATE beast_ai beast::core GTest::gtest_main)
add_test(NAME beast_ai_embed_test COMMAND beast_ai_embed_test)

# AI + 实例派发集成测试（从 platform/engine/tests/ 迁入，因依赖 ai_integration）
add_executable(beast_ai_integration_dispatch_test tests/instance_dispatch_integration_test.cpp)
target_link_libraries(beast_ai_integration_dispatch_test PRIVATE
    beast::ai_integration beast::engine beast::net GTest::gtest_main)
add_test(NAME beast_ai_integration_dispatch_test COMMAND beast_ai_integration_dispatch_test)
