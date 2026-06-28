# Beast Native Client SDK

多引擎共用的 C++ 客户端内核（Unity / UE5 / Godot GDExtension）。

## 结构

```
native/
├── core/              # beast_client_core 静态库（无引擎依赖）
├── bindings/
│   ├── c_api/         # 稳定 C ABI（Unity P/Invoke）
│   └── godot/         # Godot GDExtension 绑定
├── thirdparty/
│   └── godot-cpp/     # 由 setup 脚本克隆（不提交）
├── tools/
│   ├── setup_godot_cpp.ps1
│   └── build_godot_extension.ps1
└── CMakeLists.txt
```

Godot 侧扩展清单：`sdk/godot/beast_sdk_native/beast_sdk.gdextension`

## 构建 core + 单测（Windows）

```powershell
cmake -S sdk/native -B sdk/native/build
cmake --build sdk/native/build --config Release
ctest --test-dir sdk/native/build -C Release
```

## 构建 Godot GDExtension

```powershell
# 1. 克隆 godot-cpp（默认 godot-4.3-stable，可用 -GodotCppTag 覆盖）
sdk/native/tools/setup_godot_cpp.ps1

# 2. 一键构建并复制 DLL 到 sdk/godot/beast_sdk_native/bin/
sdk/native/tools/build_godot_extension.ps1 -Config Release
sdk/native/tools/build_godot_extension.ps1 -Config Debug
```

手动 CMake：

```powershell
cmake -S sdk/native -B sdk/native/build-godot `
  -DBEAST_CLIENT_BUILD_GODOT=ON `
  -DBEAST_CLIENT_BUILD_C_API=OFF `
  -DBEAST_CLIENT_BUILD_TESTS=OFF
cmake --build sdk/native/build-godot --config Release
cmake --build sdk/native/build-godot --config Debug
```

## Godot 使用

1. 确保 `sdk/godot/beast_sdk_native/bin/windows/` 下已有对应 debug/release DLL
2. 在 Godot 项目中引用 `beast_sdk.gdextension`（Demo 工程已包含）
3. 使用 C++ 暴露的类：
   - `BeastNativeConfig` — 连接配置 Resource
   - `BeastNativeClient` — Node，API 对齐 GDScript `BeastClient`

Native Demo：`sdk/godot/demo/main_native.gd`

## IO 线程

`Config.use_io_thread` 默认为 `true`：

- 后台线程：`TcpTransport::poll()` + 发送队列
- 主线程：`Client::poll()` 派发 connected / frame 等回调（Godot 信号安全）

关闭 IO 线程可设 `use_io_thread = false`，恢复同步 `poll()` 驱动模式。

与 `bizconfig/protocol`、`sdk/core/spec` 对齐。建房走外围 gRPC，本库仅 TCP `:8010`。

## 阶段

| 阶段 | 内容 | 状态 |
|------|------|------|
| N0 | frame_codec + wire + tests | ✅ |
| N1 | tcp_transport + Client + c_api | ✅ |
| N2 | bindings/godot (GDExtension) | ✅ |
| N3 | io_thread | ✅ |
