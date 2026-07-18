-- demo_hotlua 示例脚本
-- 通过 BEAST_HOTLUA_SCRIPT 环境变量指向本文件，或放到 scripts/hotlua/main.lua。
--
-- engine 收到 demo.hotlua.run 事件后，按 request.function_name 调用对应全局函数。
-- 参数与返回值均为字符串（demo 阶段简化协议）。

-- 启动时打印版本，确认脚本已加载
log("[hotlua] main.lua loaded, version=" .. version())

-- on_run：默认入口，原样回显参数
function on_run(arg)
    if arg == nil or arg == "" then
        return "on_run: no arg"
    end
    return "on_run: " .. arg
end

-- add：两数相加（字符串整数）
function add(a, b)
    local x = tonumber(a) or 0
    local y = tonumber(b) or 0
    return tostring(x + y)
end

-- greet：拼接问候语
function greet(name)
    return "hello, " .. (name or "world") .. "!"
end

-- now：返回当前毫秒时间戳，验证 C++ 绑定可调用
function now()
    return tostring(now_ms())
end

-- ping：简单健康检查
function ping()
    return "pong"
end
