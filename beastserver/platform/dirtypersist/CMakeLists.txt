# DirtyPersist 平台核心库：
# - 字段级 dirty tracking + debounce flush 调度
# - 抽象 IStorageBackend，支持 mongo / mysql 双后端
# - 不直接依赖任何具体 DB 驱动（mongocxx / mysql-connector-cpp 由具体 backend 源文件条件编译引入）
#
# 平台插件 plugins/dirtypersist/plugin.cpp 链接此库 + 选择一个 backend 编译。
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
