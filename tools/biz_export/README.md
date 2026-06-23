# biz_export

Go 导出器，一般**不要手敲参数**，用仓库脚本即可：

```bash
# 仓库根目录
./tools/scripts/linux/cpp-xlsx-export.sh
```

## 仅在本目录调试时

```bash
./build.sh          # 生成 ./biz_export
./biz_export --help
```

表头规范：[../excel_header_spec.md](../excel_header_spec.md)
