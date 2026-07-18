// Lua VM 实现：sol2 包装，protected_call 保证 Lua 异常不 longjmp 跳过 C++ 析构。

#include "beast/mixin/hotlua/lua_vm.hpp"

#include <sol/sol.hpp>

#include <utility>

namespace beast::mixin::hotlua {

LuaVm::LuaVm() {
    reset();
}

LuaVm::~LuaVm() = default;

LuaVm::LuaVm(LuaVm&& other) noexcept
    : lua_(std::move(other.lua_))
    , script_path_(std::move(other.script_path_))
    , last_error_(std::move(other.last_error_)) {
    other.lua_ = nullptr;
}

LuaVm& LuaVm::operator=(LuaVm&& other) noexcept {
    if (this != &other) {
        lua_ = std::move(other.lua_);
        script_path_ = std::move(other.script_path_);
        last_error_ = std::move(other.last_error_);
        other.lua_ = nullptr;
    }
    return *this;
}

void LuaVm::reset() {
    lua_ = std::make_unique<sol::state>();
    open_libs();
}

void LuaVm::open_libs() {
    // 打开标准库（base/string/table/math/io 等）
    lua_->open_libraries(sol::lib::base);
    lua_->open_libraries(sol::lib::string);
    lua_->open_libraries(sol::lib::table);
    lua_->open_libraries(sol::lib::math);
    lua_->open_libraries(sol::lib::os);
}

bool LuaVm::load_script(const std::string& path) {
    script_path_ = path;
    // protected_script_file 会在 Lua 出错时返回 sol::error，而非 longjmp
    sol::protected_function_result result = lua_->safe_script_file(path, sol::script_pass_on_error);
    if (!result.valid()) {
        sol::error err = result;
        last_error_ = std::string("load_script failed: ") + err.what();
        return false;
    }
    last_error_.clear();
    return true;
}

bool LuaVm::reload() {
    if (script_path_.empty()) {
        last_error_ = "reload failed: no script loaded yet";
        return false;
    }
    std::string saved_path = script_path_;
    reset();
    script_path_ = saved_path;
    return load_script(saved_path);
}

bool LuaVm::call_function(
    const std::string& name,
    const std::vector<std::string>& args,
    std::string& result) {
    sol::protected_function func = (*lua_)[name];
    if (!func.valid()) {
        last_error_ = "call_function failed: function '" + name + "' not found";
        return false;
    }
    // sol2 直接接受 std::string 参数，自动转 Lua 字符串
    // protected_call 会在 Lua 异常时返回 error 而非 longjmp
    sol::protected_function_result pfr;
    if (args.empty()) {
        pfr = func();
    } else if (args.size() == 1) {
        pfr = func(args[0]);
    } else if (args.size() == 2) {
        pfr = func(args[0], args[1]);
    } else {
        // 3+ 参数：展开前 3 个（demo 阶段够用）
        pfr = func(args[0], args[1], args[2]);
    }
    if (!pfr.valid()) {
        sol::error err = pfr;
        last_error_ = std::string("call_function '") + name + "' failed: " + err.what();
        return false;
    }
    // 取返回值（Lua 函数无返回时 result 为空）
    if (pfr.return_count() > 0) {
        sol::object ret = pfr;
        if (ret.is<std::string>()) {
            result = ret.as<std::string>();
        } else if (ret.is<int>()) {
            result = std::to_string(ret.as<int>());
        } else if (ret.is<double>()) {
            result = std::to_string(ret.as<double>());
        } else if (ret.is<bool>()) {
            result = ret.as<bool>() ? "true" : "false";
        } else {
            result = "nil";
        }
    } else {
        result.clear();
    }
    last_error_.clear();
    return true;
}

bool LuaVm::call_function(const std::string& name, std::string& result) {
    return this->call_function(name, std::vector<std::string>{}, result);
}

void LuaVm::register_function(
    const std::string& name,
    std::function<void(const std::string&)> fn) {
    // 把 std::function 包成 Lua 可调用的 C function
    (*lua_)[name] = std::move(fn);
}

sol::state& LuaVm::state() noexcept {
    return *lua_;
}

const sol::state& LuaVm::state() const noexcept {
    return *lua_;
}

} // namespace beast::mixin::hotlua
