# 常见问题

## 启动卡在 `GameServer starting`，无 `PluginHost loaded`

**现象**：进程存活、主线程 CPU 接近 100%，日志不再刷新。

**原因**：`beastserver` 已重编，但 `gameplays/` 下 `.so` 仍是修改 `gameplays/` **之前**的旧产物，`dlopen` 加载时玩法层 ABI 不一致。

**处理**：

```bash
cd beastserver/build/RelWithDebInfo
cmake --build . --target beastserver -j$(nproc)
./beastserver
```

或显式重编插件：

```bash
cmake --build . --target beast_plugin_demo_event beast_plugin_demo_tick -j$(nproc)
```

**规避**：改 `platform/` 后始终用 `--target beastserver` 或全量构建；勿只拷贝旧 `.so` 到新构建目录。

## 临时跳过动态插件

配置中设 `"auto_load": false`（见 `config/server.json` 的 `plugins` 段），或在代码里 `register_static_plugin` 静态注册引擎，用于不依赖 `dlopen` 的调试。

## 插件 .so 找不到

```bash
# 开发时 .so 在 build 目录，用环境变量覆盖
BEAST_PLUGINS_DIR=build/RelWithDebInfo/plugins ./build/RelWithDebInfo/beastserver
```

或检查 `config/server.json` 中 `plugins.dir` 是否指向正确目录。

## 生产环境启动失败：TLS / 加密未启用

当 `server.debug.enabled = false` 时：

- TCP 必须启用 TLS（`net.tcp.tls.enabled = true`，且 `cert_path` / `key_path` 非空）
- KCP 必须启用加密（`net.kcp.crypto.mode = "psk_aes_gcm"`，`tag_bytes` 8-16）
- WebSocket Origin 白名单必须非空

开发阶段保持 `debug.enabled = true` 可跳过这些检查。详见 [传输层](transport.md)。

## 测试失败：config_test

`ServerConfigTest.LoadsExampleServerJson` 检查 `server.json` 与测试期望一致。若修改了 `server.json`（如切换 `bizconfig.enabled`），需同步更新 `platform/core/tests/config_test.cpp` 中的期望值。

## 测试失败：outbound_test

`OutboundHubTest.PreferTcpDoesNotFallbackToOtherProtocol` 是已知测试用例与当前 `Session::select_channel` 回退行为不一致的问题。`PreferTcp` / `PreferKcp` 策略在首选协议不可用时会回退到 `find_any()`，而测试期望严格不回退。使用 `PreferTcpOnly` / `PreferKcpOnly` 可实现严格选择。
