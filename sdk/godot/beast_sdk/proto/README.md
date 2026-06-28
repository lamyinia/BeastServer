# Proto 源文件不在此目录。

唯一真相源：

```
BeastServer-project/bizconfig/protocol/
```

Platform 生成物见 `../generated/`：

- `auth_routes.gd` — `gen_routes_from_proto.py`
- message 类 — `gen_messages_from_proto.py`（**protoc** descriptor + WireCodec emit）

依赖：`pip install -r sdk/tools/requirements.txt`，PATH 中有 `protoc`。

生成：`sdk/tools/gen_proto_godot.ps1` 或 `.sh`（详见 `sdk/tools/proto_codegen.md`）
