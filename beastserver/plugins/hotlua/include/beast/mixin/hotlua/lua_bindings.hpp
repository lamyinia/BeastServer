// Lua C++ 绑定：把平台 C++ 函数注册到 Lua 全局表，供脚本回调。

#pragma once

// 前向声明 sol::state，避免头文件强依赖 sol2
namespace sol {
class state;
} // namespace sol

namespace beast::mixin::hotlua {

// 注册默认 C++ 绑定到 Lua state：
//   log(msg)        — 日志输出（走 BEAST_LOG_INFO）
//   echo(msg)       — 返回 msg 原文（测试用）
//   version()       — 返回框架版本字符串
//   now_ms()        — 返回当前时间戳（毫秒）
void register_default_bindings(sol::state& lua);

} // namespace beast::mixin::hotlua
