#!/usr/bin/env python3
"""Parse // route: comments from .proto and emit Godot routes.gd (or JSON)."""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

# 注意：不再支持 engine_route alias（第 4 段）。
# InstanceEvent::route 始终存 wire_route，engine on_event 宏也用 wire_route 字符串匹配。
ROUTE_LINE = re.compile(
    r"^\s*//\s*route:(?P<msg>[A-Za-z_]\w*)\|(?P<dir>[a-z][a-z0-9_]*)\|(?P<wire>[^\s|]+)\s*$"
)
MESSAGE_LINE = re.compile(r"^\s*message\s+(?P<name>[A-Za-z_]\w*)\s*\{")

# plugin.cpp 里 register_instance_route 调用形式（简化后只匹配 wire_route）：
#   register_instance_route<T>(ctx, "wire_route")
#   register_instance_route<T>(ctx, "wire_route", make_payload)
# 非模板版本：
#   register_instance_route(ctx, "wire_route")
PLUGIN_C2S = re.compile(
    r'register_instance_route(?:<\s*[\w:]+\s*>)?\(\s*ctx,\s*"([^"]+)"'
)


@dataclass(frozen=True)
class RouteEntry:
    message: str
    direction: str
    wire_route: str
    proto_file: str
    line_no: int


def snake_to_screaming(name: str) -> str:
    out: list[str] = []
    for i, ch in enumerate(name):
        if ch.isupper() and i > 0 and (name[i - 1].islower() or (i + 1 < len(name) and name[i + 1].islower())):
            out.append("_")
        out.append(ch.upper())
    return "".join(out)


def parse_proto_routes(proto_path: Path) -> list[RouteEntry]:
    text = proto_path.read_text(encoding="utf-8")
    lines = text.splitlines()
    entries: list[RouteEntry] = []

    for idx, line in enumerate(lines):
        line = line.rstrip("\r\n")
        match = ROUTE_LINE.match(line)
        if not match:
            # 兼容历史 proto：如果注释带了第 4 段 engine_route alias，正则不匹配
            # 但仍然给出明确报错，提示调用方更新 proto 注释
            legacy = re.match(
                r"^\s*//\s*route:[A-Za-z_]\w*\|[a-z][a-z0-9_]*\|[^\s|]+\|[^\s|]+\s*$",
                line,
            )
            if legacy:
                raise ValueError(
                    f"{proto_path}:{idx + 1}: route comment has legacy engine_route alias "
                    f"(4th `|`-separated field is no longer supported): {line!r}. "
                    f"Remove the 4th field; InstanceEvent::route now stores wire_route directly."
                )
            continue

        msg_line = lines[idx + 1].rstrip("\r\n") if idx + 1 < len(lines) else ""
        msg_match = MESSAGE_LINE.match(msg_line)
        if not msg_match:
            raise ValueError(f"{proto_path}:{idx + 1}: route comment not followed by message")
        if msg_match.group("name") != match.group("msg"):
            raise ValueError(
                f"{proto_path}:{idx + 1}: route message {match.group('msg')!r} "
                f"!= {msg_match.group('name')!r}"
            )

        entries.append(
            RouteEntry(
                message=match.group("msg"),
                direction=match.group("dir"),
                wire_route=match.group("wire"),
                proto_file=str(proto_path),
                line_no=idx + 1,
            )
        )

    return entries


def emit_gdscript(entries: Iterable[RouteEntry], class_name: str, proto_path: Path) -> str:
    lines = [
        f"class_name {class_name}",
        "extends RefCounted",
        f"## Generated from {proto_path.as_posix()} — do not edit",
        "",
    ]
    for entry in entries:
        const_name = snake_to_screaming(entry.message)
        lines.append(f"const {const_name} := \"{entry.wire_route}\"  ## {entry.direction}")
    lines.append("")
    return "\n".join(lines)


def parse_plugin_c2s(plugin_path: Path) -> list[str]:
    """从 plugin.cpp 提取所有 register_instance_route 的 wire_route 字符串。"""
    text = plugin_path.read_text(encoding="utf-8")
    return PLUGIN_C2S.findall(text)


def verify_plugin(entries: list[RouteEntry], plugin_path: Path) -> list[str]:
    errors: list[str] = []
    c2s_entries = [e for e in entries if e.direction in ("c2s", "c2s_resp")]
    expected = {e.wire_route for e in c2s_entries}
    actual = set(parse_plugin_c2s(plugin_path))

    missing = expected - actual
    extra = actual - expected
    for wire in sorted(missing):
        errors.append(f"plugin missing register_instance_route wire={wire!r}")
    for wire in sorted(extra):
        errors.append(f"plugin extra register_instance_route wire={wire!r} (not in proto route comments)")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate route constants from proto // route: comments")
    parser.add_argument("--proto", type=Path, required=True, help="Path to .proto file")
    parser.add_argument("--out", type=Path, help="Output .gd path")
    parser.add_argument("--json-out", type=Path, help="Optional JSON manifest output")
    parser.add_argument("--class-name", default="", help="Godot class_name (default: derived from proto stem)")
    parser.add_argument("--verify-plugin", type=Path, help="Verify c2s routes against plugin.cpp")
    parser.add_argument(
        "--skip-if-no-routes",
        action="store_true",
        help="Exit 0 when proto has no // route: entries (batch sync)",
    )
    args = parser.parse_args()

    if not args.proto.is_file():
        print(f"proto not found: {args.proto}", file=sys.stderr)
        return 1

    entries = parse_proto_routes(args.proto)
    if not entries:
        msg = f"no // route: entries in {args.proto}"
        if args.skip_if_no_routes:
            print(f"SKIP: {msg}")
            return 0
        print(msg, file=sys.stderr)
        return 1

    class_name = args.class_name or ("".join(part.title() for part in args.proto.stem.split("_")) + "Routes")

    if args.verify_plugin:
        errors = verify_plugin(entries, args.verify_plugin)
        if errors:
            for err in errors:
                print(f"VERIFY FAIL: {err}", file=sys.stderr)
            return 1
        print(f"VERIFY OK: {args.proto.name} vs {args.verify_plugin.name}")

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        payload = [
            {
                "message": e.message,
                "direction": e.direction,
                "wire_route": e.wire_route,
                "proto_file": e.proto_file,
                "line": e.line_no,
            }
            for e in entries
        ]
        args.json_out.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"Wrote {args.json_out}")

    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        gd = emit_gdscript(entries, class_name, args.proto)
        args.out.write_text(gd, encoding="utf-8")
        print(f"Wrote {args.out} ({len(entries)} routes)")

    if not args.out and not args.json_out and not args.verify_plugin:
        print(json.dumps([e.__dict__ for e in entries], indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
