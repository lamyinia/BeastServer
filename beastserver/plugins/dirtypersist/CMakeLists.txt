# DirtyPersist 平台核心库 + 集成层 + 插件
#
# 合并自原 platform/dirtypersist/（服务实现）和 plugins/dirtypersist/（插件注册 + facade）
#
# 核心库 beast::dirtypersist：
# - 字段级 dirty tracking + debounce flush 调度
# - 抽象 IStorageBackend，支持 mongo / mysql 双后端
# - 不直接依赖任何具体 DB 驱动（mongocxx / mysql-connector-cpp 由具体 backend 源文件条件编译引入）
#
# 集成层 beast::dirtypersist_integration（INTERFACE）：
# - InstanceDirtyPersistFacade（玩法层入口），命名空间 beast::mixin::dirtypersist
#
# 平台插件 platform_dirtypersist：
# - 将 DirtyPersistService + InstanceDirtyPersistFacade 注册到 ServiceRegistry
# - 输出为 build/plugins/beast_plugin_platform_dirtypersist.so，PluginHost Phase 1 扫描加载

find_package(Boost REQUIRED)

beast_add_library(dirtypersist
    GLOB src/*.cpp
    DEPS beast::core Boost::boost)

# Mongo backend：依赖 mongocxx（条件编译，由 plugin 侧决定是否启用）
# 启用方式：cmake -DBEAST_DIRTYPERSIST_WITH_MONGO=ON
# 同时需在 conanfile.txt 加 mongo-cxx-driver/3.10 依赖
option(BEAST_DIRTYPERSIST_WITH_MONGO "Build MongoBackend for DirtyPersist" OFF)
if(BEAST_DIRTYPERSIST_WITH_MONGO)
    find_package(mongocxx REQUIRED)
    target_sources(beast_dirtypersist PRIVATE src/mongo_backend.cpp)
    target_link_libraries(beast_dirtypersist PRIVATE mongo::mongocxx_shared)
    target_compile_definitions(beast_dirtypersist PUBLIC BEAST_DIRTYPERSIST_WITH_MONGO=1)
endif()

# MySQL backend：用 Boost.MySQL（boost/1.86.0 自带，header-only）
# 启用方式：cmake -DBEAST_DIRTYPERSIST_WITH_MYSQL=ON
# Boost.MySQL 原生 Boost.Asio async，不需要 db_pool 线程池桥接
#
# ⚠️ Boost.MySQL 虽然本身 header-only，但它会引用 Boost.Asio SSL 和 Boost.Charconv
#    两个需要单独链接的依赖：
#    1. Boost.Asio SSL（boost/asio/ssl/）-> 链接 OpenSSL::SSL OpenSSL::Crypto
#       即使 ssl_mode::disable，模板代码也会引用 SSL_CTX_free 等符号
#    2. Boost.Charconv::to_chars（double/float 格式化）-> 链接 Boost::charconv
#       format_sql 格式化浮点数时用到，非模板实现需要单独编译的库
#    Boost::headers 是 header-only 的接口，不传递这些传递性依赖。
#    不显式链接会导致 .so 编译能过（链接器默认 --allow-shlib-undefined）但
#    dlopen 时失败 with "undefined symbol: SSL_CTX_free" 或 "to_chars"。
option(BEAST_DIRTYPERSIST_WITH_MYSQL "Build MysqlBackend for DirtyPersist" OFF)
if(BEAST_DIRTYPERSIST_WITH_MYSQL)
    find_package(OpenSSL REQUIRED)
    find_package(Boost REQUIRED COMPONENTS charconv)
    target_sources(beast_dirtypersist PRIVATE src/mysql_backend.cpp)
    target_compile_definitions(beast_dirtypersist PUBLIC BEAST_DIRTYPERSIST_WITH_MYSQL=1)
    target_link_libraries(beast_dirtypersist PRIVATE
        OpenSSL::SSL OpenSSL::Crypto
        Boost::charconv)
endif()

enable_testing()

# 单元测试：DirtyTracker 与 EntityTraits 宏（不依赖具体 backend）
beast_add_gtest(dirtypersist_tracker
    SOURCES tests/dirty_tracker_test.cpp
    DEPS beast::dirtypersist)

beast_add_gtest(dirtypersist_entity_traits
    SOURCES tests/entity_traits_test.cpp
    DEPS beast::dirtypersist)

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
