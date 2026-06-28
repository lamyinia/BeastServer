# Proto → Godot 生成工具链

缺口与待办详见 [`proto_toolchain_gaps.md`](proto_toolchain_gaps.md)。

## 分工

| 产物 | 脚本 | 解析 |
|------|------|------|
| `*_routes.gd` | `gen_routes_from_proto.py` | proto 内 `// route:` 注释 |
| `{message}.gd` | `gen_messages_from_proto.py` | **protoc** descriptor → 自研 emit（WireCodec） |

route 与 message **解耦**：route 是 Beast 项目约定；message 编解码走官方 protoc AST，避免正则解析 proto。

## 依赖

```bash
pip install -r sdk/tools/requirements.txt   # google.protobuf
# protoc 已在 PATH，或设置环境变量 PROTOC
```

## 入口

| 场景 | 命令 |
|------|------|
| SDK platform | `sdk/tools/gen_proto_godot.ps1` / `.sh` |
| SDK demo | `sdk/tools/sync_demo_generated.ps1` |
| pixel-moba | `pixel-moba/infra/tools/sync_infra.ps1` |

## 生成物约定

- class_name：`Beast{MessageName}`（默认 prefix）
- API：`to_bytes()` / `static from_bytes()` → null
- runtime：`beast_sdk/impl/codec/wire_codec.gd`

## 当前支持 / 未支持

**支持：**

- scalar：string、bytes、bool、int32/64、uint32/64、sint32/64（zigzag）、float/double、fixed32/64、sfixed32/64
- `repeated`（含 scalar / enum / message；数值型 proto3 默认 **packed**）
- 嵌套 message（扁平化为 `Beast{Parent}{Child}`，如 `Player.Position` → `BeastPlayerPosition`）
- enum（独立 `{enum}.gd` const 类，字段类型为 `int`）

**未支持：**

- proto2 语法、`oneof`、`map`
- proto `group` 类型
- `[packed = false]` 显式非 packed 的 encode 仍走 packed（decode 兼容 unpacked）

出现上述未支持类型时，先扩展 `WireCodec` + emit 模板。
