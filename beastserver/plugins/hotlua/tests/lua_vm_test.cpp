// LuaVm 单元测试：加载/调用/重载/错误处理/绑定

#include "beast/mixin/hotlua/lua_bindings.hpp"
#include "beast/mixin/hotlua/lua_vm.hpp"
#include "beast/mixin/hotlua/lua_vm_service.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

using beast::mixin::hotlua::LuaVm;
using beast::mixin::hotlua::LuaVmService;
using beast::mixin::hotlua::register_default_bindings;

namespace {

// 临时 Lua 脚本文件 helper：构造时写内容，析构时删除。
class TempLuaScript {
public:
    explicit TempLuaScript(const std::string& content) {
        path_ = std::string(std::tmpnam(nullptr)) + ".lua";
        std::ofstream f(path_);
        f << content;
    }
    ~TempLuaScript() { std::remove(path_.c_str()); }
    TempLuaScript(const TempLuaScript&) = delete;
    TempLuaScript& operator=(const TempLuaScript&) = delete;
    [[nodiscard]] const std::string& path() const noexcept { return path_; }

    // 覆盖写入新内容（测热重载用）
    void rewrite(const std::string& content) {
        std::ofstream f(path_, std::ios::trunc);
        f << content;
    }

private:
    std::string path_;
};

} // namespace

// ========== 基础加载与调用 ==========

TEST(LuaVmTest, LoadAndCallReturnsValue) {
    TempLuaScript script(R"(
        function add(a, b)
            return tostring(a + b)
        end
    )");
    beast::mixin::hotlua::LuaVm vm;
    ASSERT_TRUE(vm.load_script(script.path())) << vm.last_error();

    std::string result;
    // 注意：当前 call_function 传参走 create_string，单参数路径
    ASSERT_TRUE(vm.call_function("add", {"3", "4"}, result)) << vm.last_error();
    EXPECT_EQ(result, "7");
}

TEST(LuaVmTest, CallNoArgFunction) {
    TempLuaScript script(R"(
        function ping()
            return "pong"
        end
    )");
    beast::mixin::hotlua::LuaVm vm;
    ASSERT_TRUE(vm.load_script(script.path())) << vm.last_error();

    std::string result;
    ASSERT_TRUE(vm.call_function("ping", result)) << vm.last_error();
    EXPECT_EQ(result, "pong");
}

TEST(LuaVmTest, CallNonexistentFunctionFails) {
    TempLuaScript script(R"(
        function exists()
            return "yes"
        end
    )");
    beast::mixin::hotlua::LuaVm vm;
    ASSERT_TRUE(vm.load_script(script.path()));

    std::string result;
    EXPECT_FALSE(vm.call_function("nonexistent", result));
    EXPECT_NE(vm.last_error().find("not found"), std::string::npos);
}

TEST(LuaVmTest, ScriptSyntaxErrorFails) {
    TempLuaScript script(R"(
        function broken(
            -- 语法错误
    )");
    beast::mixin::hotlua::LuaVm vm;
    EXPECT_FALSE(vm.load_script(script.path()));
    EXPECT_FALSE(vm.last_error().empty());
}

TEST(LuaVmTest, RuntimeErrorCaught) {
    TempLuaScript script(R"(
        function boom()
            error("intentional crash")
        end
    )");
    beast::mixin::hotlua::LuaVm vm;
    ASSERT_TRUE(vm.load_script(script.path()));

    std::string result;
    EXPECT_FALSE(vm.call_function("boom", result));
    EXPECT_NE(vm.last_error().find("intentional crash"), std::string::npos);
}

// ========== 热重载 ==========

TEST(LuaVmTest, ReloadPicksUpNewBehavior) {
    TempLuaScript script(R"(
        function get_value()
            return "before"
        end
    )");
    beast::mixin::hotlua::LuaVm vm;
    ASSERT_TRUE(vm.load_script(script.path()));

    std::string result;
    ASSERT_TRUE(vm.call_function("get_value", result));
    EXPECT_EQ(result, "before");

    // 改脚本内容
    script.rewrite(R"(
        function get_value()
            return "after"
        end
    )");

    // 重载前仍是旧行为
    ASSERT_TRUE(vm.call_function("get_value", result));
    EXPECT_EQ(result, "before");

    // 重载后变新行为
    ASSERT_TRUE(vm.reload()) << vm.last_error();
    ASSERT_TRUE(vm.call_function("get_value", result));
    EXPECT_EQ(result, "after");
}

TEST(LuaVmTest, ReloadWithoutLoadFails) {
    beast::mixin::hotlua::LuaVm vm;
    EXPECT_FALSE(vm.reload());
    EXPECT_NE(vm.last_error().find("no script loaded"), std::string::npos);
}

// ========== C++ 绑定 ==========

TEST(LuaVmTest, CppBindingEchoCallable) {
    TempLuaScript script(R"(
        function test_echo()
            return echo("hello from lua")
        end
    )");
    beast::mixin::hotlua::LuaVm vm;
    register_default_bindings(vm.state());
    ASSERT_TRUE(vm.load_script(script.path()));

    std::string result;
    ASSERT_TRUE(vm.call_function("test_echo", result)) << vm.last_error();
    EXPECT_EQ(result, "hello from lua");
}

TEST(LuaVmTest, CppBindingVersionCallable) {
    TempLuaScript script(R"(
        function get_ver()
            return version()
        end
    )");
    beast::mixin::hotlua::LuaVm vm;
    register_default_bindings(vm.state());
    ASSERT_TRUE(vm.load_script(script.path()));

    std::string result;
    ASSERT_TRUE(vm.call_function("get_ver", result)) << vm.last_error();
    EXPECT_NE(result.find("BeastServer"), std::string::npos);
}

TEST(LuaVmTest, CppBindingNowMsCallable) {
    TempLuaScript script(R"(
        function get_time()
            return tostring(now_ms())
        end
    )");
    beast::mixin::hotlua::LuaVm vm;
    register_default_bindings(vm.state());
    ASSERT_TRUE(vm.load_script(script.path()));

    std::string result;
    ASSERT_TRUE(vm.call_function("get_time", result)) << vm.last_error();
    // 返回值应是一个数字字符串
    EXPECT_FALSE(result.empty());
    EXPECT_NE(result, "nil");
}

// ========== LuaVmService ==========

TEST(LuaVmServiceTest, CreateVmHasDefaultBindings) {
    beast::mixin::hotlua::LuaVmService service;
    auto vm = service.create_vm();

    TempLuaScript script(R"(
        function test()
            return echo("service works")
        end
    )");
    ASSERT_TRUE(vm->load_script(script.path()));

    std::string result;
    ASSERT_TRUE(vm->call_function("test", result));
    EXPECT_EQ(result, "service works");
}

TEST(LuaVmServiceTest, CreateVmMultipleIndependent) {
    beast::mixin::hotlua::LuaVmService service;
    auto vm1 = service.create_vm();
    auto vm2 = service.create_vm();

    TempLuaScript script1(R"(
        function who()
            return "vm1"
        end
    )");
    TempLuaScript script2(R"(
        function who()
            return "vm2"
        end
    )");
    ASSERT_TRUE(vm1->load_script(script1.path()));
    ASSERT_TRUE(vm2->load_script(script2.path()));

    std::string r1, r2;
    ASSERT_TRUE(vm1->call_function("who", r1));
    ASSERT_TRUE(vm2->call_function("who", r2));
    EXPECT_EQ(r1, "vm1");
    EXPECT_EQ(r2, "vm2");
}
