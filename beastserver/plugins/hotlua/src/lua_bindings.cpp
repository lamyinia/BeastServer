// Lua 绑定实现：注册 C++ 函数到 Lua。

#include "beast/mixin/hotlua/lua_bindings.hpp"

#include "beast/platform/core/log/logger.hpp"

#include <sol/sol.hpp>

#include <chrono>

namespace beast::mixin::hotlua {

namespace {

// Lua 可调用的 log 函数
int lua_log(const std::string& msg) {
    BEAST_LOG_INFO("[lua] {}", msg);
    return 0;
}

// Lua 可调用的 echo 函数（测试用，原样返回）
std::string lua_echo(const std::string& msg) {
    return msg;
}

// 返回框架版本
std::string lua_version() {
    return "BeastServer/0.1.0-hotlua";
}

// 返回当前时间戳（毫秒）
long long lua_now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

} // namespace

void register_default_bindings(sol::state& lua) {
    lua["log"] = lua_log;
    lua["echo"] = lua_echo;
    lua["version"] = lua_version;
    lua["now_ms"] = lua_now_ms;
}

} // namespace beast::mixin::hotlua
