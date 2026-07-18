# 策划表导出（tools）

改 Excel → 一条命令导出 → 服务端读 `*_s.pb`。

## 第一次（装工具）

```bash
cd BeastServer-project/tools/biz_export
./build.sh
```

需要：Go 1.22+、`protoc`（系统里能跑 `protoc --version` 即可）。

## 日常（改完表就跑）

在**仓库根目录**执行：

```bash
./tools/scripts/linux/cpp-xlsx-export.sh
```

输入：`bizconfig/static-xlsx/**/*.xlsx`  
输出：

| 路径 | 用途 |
|------|------|
| `beastserver/build/bizconfig/server/` | 服务端 `*_s.pb` |
| `beastserver/build/bizconfig/client/` | 客户端 `*_c.pb` |
| `beastserver/build/bizconfig/manifest.json` | 版本与校验 |
| `bizconfig/scheme/` | 策划表 schema（工具生成 `.proto`） |

## 跑服验证

```bash
cd beastserver
./build/beastserver config/server.json
```

`server.json` 里需 `bizconfig.enabled: true`（仓库默认已开）。

## 表头怎么写

见 [excel_header_spec.md](./excel_header_spec.md)（Row1 字段+类型，Row2 约束/`!s!c`）。

样例 Excel：`bizconfig/static-xlsx/example/npc/npc.xlsx`、`bizconfig/static-xlsx/moba/pixel_moba/hero.xlsx`。

## 常见问题

**脚本报 `biz_export not built`** → 先执行上面「第一次」的 `./build.sh`。

**改了 xlsx 但服里还是旧数据** → 再跑一遍 `cpp-xlsx-export.sh`，然后重启 beastserver。

**Windows** → `.\tools\scripts\win\xlsx-export.ps1`

**Godot 同步（pixel-moba）** → 先 xlsx-export，再 `pixel-moba/infra/tools/sync_infra.ps1 -SyncBiz`
