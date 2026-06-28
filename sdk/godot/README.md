# Godot 集成

## Addon 路径

```
sdk/godot/beast_sdk/
```

在目标 Godot 项目中：

1. 复制或链接到 `addons/beast_sdk/`
2. Project Settings → Plugins → 启用 **Beast SDK**

## Demo

```
sdk/godot/demo/main.tscn        # GDScript：grpc 建房 + TCP login + ping2
sdk/godot/demo/main_native.gd   # GDExtension Native 版（需先构建 beast_sdk_native）
```

## GDExtension（Native）

```powershell
sdk/native/tools/build_godot_extension.ps1 -Config Release
sdk/native/tools/build_godot_extension.ps1 -Config Debug
```

扩展清单：`sdk/godot/beast_sdk_native/beast_sdk.gdextension`

暴露类：`BeastNativeConfig`、`BeastNativeClient`（API 对齐 `BeastClient`）

## 测试

```bash
godot --headless --path sdk/godot --script res://beast_sdk/tests/run_m3_tests.gd
```

## Demo（需先 grpcurl 建房）

```bash
godot --path sdk/godot --main-pack res://demo/main.tscn
# 或在编辑器打开 sdk/godot，运行 demo/main.tscn，Inspector 设置 host/token
```
