#!/usr/bin/env python3
"""Generate Godot WireCodec message classes from proto3 (protoc descriptor + emit)."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

try:
    from google.protobuf import descriptor_pb2
except ImportError as exc:
    raise SystemExit(
        "missing dependency: pip install -r sdk/tools/requirements.txt"
    ) from exc

# --- scalar emit templates ---------------------------------------------------

TYPE_INFO = {
    "string": {
        "gd_type": "String",
        "repeated_gd_type": "Array[String]",
        "default": '""',
        "repeated_default": "[]",
        "encode_empty_check": "not {field}.is_empty()",
        "encode_call": "_WireCodec.encode_string_field({num}, {field})",
        "decode_call": "_WireCodec.decode_string_field(data, offset, key.wire_type)",
    },
    "bytes": {
        "gd_type": "PackedByteArray",
        "repeated_gd_type": "Array[PackedByteArray]",
        "default": "PackedByteArray()",
        "repeated_default": "[]",
        "encode_empty_check": "not {field}.is_empty()",
        "encode_call": "_WireCodec.encode_bytes_field({num}, {field})",
        "decode_call": "_decode_bytes_field(data, offset, key.wire_type)",
    },
    "bool": {
        "gd_type": "bool",
        "repeated_gd_type": "Array[bool]",
        "default": "false",
        "repeated_default": "[]",
        "encode_empty_check": None,
        "encode_call": "_WireCodec.encode_bool_field({num}, {field})",
        "decode_call": "_WireCodec.decode_bool_field(data, offset, key.wire_type)",
        "packed_codec": "varint",
    },
    "int32": {
        "gd_type": "int",
        "repeated_gd_type": "Array[int]",
        "default": "0",
        "repeated_default": "[]",
        "encode_empty_check": "{field} != 0",
        "encode_call": "_WireCodec.encode_uint64_field({num}, {field})",
        "decode_call": "_WireCodec.decode_uint64_field(data, offset, key.wire_type)",
        "packed_codec": "varint",
    },
    "int64": {
        "gd_type": "int",
        "repeated_gd_type": "Array[int]",
        "default": "0",
        "repeated_default": "[]",
        "encode_empty_check": "{field} != 0",
        "encode_call": "_WireCodec.encode_uint64_field({num}, {field})",
        "decode_call": "_WireCodec.decode_uint64_field(data, offset, key.wire_type)",
        "packed_codec": "varint",
    },
    "uint32": {
        "gd_type": "int",
        "repeated_gd_type": "Array[int]",
        "default": "0",
        "repeated_default": "[]",
        "encode_empty_check": "{field} != 0",
        "encode_call": "_WireCodec.encode_uint64_field({num}, {field})",
        "decode_call": "_WireCodec.decode_uint64_field(data, offset, key.wire_type)",
        "packed_codec": "varint",
    },
    "uint64": {
        "gd_type": "int",
        "repeated_gd_type": "Array[int]",
        "default": "0",
        "repeated_default": "[]",
        "encode_empty_check": "{field} != 0",
        "encode_call": "_WireCodec.encode_uint64_field({num}, {field})",
        "decode_call": "_WireCodec.decode_uint64_field(data, offset, key.wire_type)",
        "packed_codec": "varint",
    },
    "sint32": {
        "gd_type": "int",
        "repeated_gd_type": "Array[int]",
        "default": "0",
        "repeated_default": "[]",
        "encode_empty_check": "{field} != 0",
        "encode_call": "_WireCodec.encode_sint32_field({num}, {field})",
        "decode_call": "_WireCodec.decode_sint32_field(data, offset, key.wire_type)",
        "packed_codec": "sint32",
    },
    "sint64": {
        "gd_type": "int",
        "repeated_gd_type": "Array[int]",
        "default": "0",
        "repeated_default": "[]",
        "encode_empty_check": "{field} != 0",
        "encode_call": "_WireCodec.encode_sint64_field({num}, {field})",
        "decode_call": "_WireCodec.decode_sint64_field(data, offset, key.wire_type)",
        "packed_codec": "sint64",
    },
    "float": {
        "gd_type": "float",
        "repeated_gd_type": "Array[float]",
        "default": "0.0",
        "repeated_default": "[]",
        "encode_empty_check": "{field} != 0.0",
        "encode_call": "_WireCodec.encode_float_field({num}, {field})",
        "decode_call": "_WireCodec.decode_float_field(data, offset, key.wire_type)",
        "packed_codec": "float",
    },
    "double": {
        "gd_type": "float",
        "repeated_gd_type": "Array[float]",
        "default": "0.0",
        "repeated_default": "[]",
        "encode_empty_check": "{field} != 0.0",
        "encode_call": "_WireCodec.encode_double_field({num}, {field})",
        "decode_call": "_WireCodec.decode_double_field(data, offset, key.wire_type)",
        "packed_codec": "double",
    },
    "fixed32": {
        "gd_type": "int",
        "repeated_gd_type": "Array[int]",
        "default": "0",
        "repeated_default": "[]",
        "encode_empty_check": "{field} != 0",
        "encode_call": "_WireCodec.encode_fixed32_field({num}, {field})",
        "decode_call": "_WireCodec.decode_fixed32_field(data, offset, key.wire_type)",
        "packed_codec": "fixed32",
    },
    "fixed64": {
        "gd_type": "int",
        "repeated_gd_type": "Array[int]",
        "default": "0",
        "repeated_default": "[]",
        "encode_empty_check": "{field} != 0",
        "encode_call": "_WireCodec.encode_fixed64_field({num}, {field})",
        "decode_call": "_WireCodec.decode_fixed64_field(data, offset, key.wire_type)",
        "packed_codec": "fixed64",
    },
    "sfixed32": {
        "gd_type": "int",
        "repeated_gd_type": "Array[int]",
        "default": "0",
        "repeated_default": "[]",
        "encode_empty_check": "{field} != 0",
        "encode_call": "_WireCodec.encode_fixed32_field({num}, {field})",
        "decode_call": "_WireCodec.decode_fixed32_field(data, offset, key.wire_type)",
        "packed_codec": "fixed32",
    },
    "sfixed64": {
        "gd_type": "int",
        "repeated_gd_type": "Array[int]",
        "default": "0",
        "repeated_default": "[]",
        "encode_empty_check": "{field} != 0",
        "encode_call": "_WireCodec.encode_fixed64_field({num}, {field})",
        "decode_call": "_WireCodec.decode_fixed64_field(data, offset, key.wire_type)",
        "packed_codec": "fixed64",
    },
}

PACKED_WIRECODE = {
    "varint": ("encode_packed_varint_field", "decode_packed_varint_chunk"),
    "sint32": ("encode_packed_sint32_field", "decode_packed_sint32_chunk"),
    "sint64": ("encode_packed_sint64_field", "decode_packed_sint64_chunk"),
    "fixed32": ("encode_packed_fixed32_field", "decode_packed_fixed32_chunk"),
    "float": ("encode_packed_float_field", "decode_packed_float_chunk"),
    "fixed64": ("encode_packed_fixed64_field", "decode_packed_fixed64_chunk"),
    "double": ("encode_packed_double_field", "decode_packed_double_chunk"),
}

NON_PACKABLE_FIELD_TYPES = frozenset(
    {
        descriptor_pb2.FieldDescriptorProto.TYPE_STRING,
        descriptor_pb2.FieldDescriptorProto.TYPE_BYTES,
        descriptor_pb2.FieldDescriptorProto.TYPE_MESSAGE,
        descriptor_pb2.FieldDescriptorProto.TYPE_GROUP,
    }
)

SCALAR_TYPE_MAP = {
    descriptor_pb2.FieldDescriptorProto.TYPE_DOUBLE: "double",
    descriptor_pb2.FieldDescriptorProto.TYPE_FLOAT: "float",
    descriptor_pb2.FieldDescriptorProto.TYPE_INT64: "int64",
    descriptor_pb2.FieldDescriptorProto.TYPE_UINT64: "uint64",
    descriptor_pb2.FieldDescriptorProto.TYPE_INT32: "int32",
    descriptor_pb2.FieldDescriptorProto.TYPE_FIXED64: "fixed64",
    descriptor_pb2.FieldDescriptorProto.TYPE_FIXED32: "fixed32",
    descriptor_pb2.FieldDescriptorProto.TYPE_BOOL: "bool",
    descriptor_pb2.FieldDescriptorProto.TYPE_STRING: "string",
    descriptor_pb2.FieldDescriptorProto.TYPE_GROUP: None,
    descriptor_pb2.FieldDescriptorProto.TYPE_MESSAGE: None,
    descriptor_pb2.FieldDescriptorProto.TYPE_BYTES: "bytes",
    descriptor_pb2.FieldDescriptorProto.TYPE_UINT32: "uint32",
    descriptor_pb2.FieldDescriptorProto.TYPE_ENUM: None,
    descriptor_pb2.FieldDescriptorProto.TYPE_SFIXED32: "sfixed32",
    descriptor_pb2.FieldDescriptorProto.TYPE_SFIXED64: "sfixed64",
    descriptor_pb2.FieldDescriptorProto.TYPE_SINT32: "sint32",
    descriptor_pb2.FieldDescriptorProto.TYPE_SINT64: "sint64",
}


@dataclass(frozen=True)
class ProtoEnum:
    name: str
    class_name: str
    file_stem: str
    values: tuple[tuple[str, int], ...]
    source_file: str = ""


@dataclass(frozen=True)
class ProtoMessage:
    name: str
    class_name: str
    file_stem: str
    fields: tuple[ProtoField, ...]
    source_file: str = ""


@dataclass(frozen=True)
class ProtoField:
    name: str
    number: int
    repeated: bool
    kind: str  # scalar | enum | message
    scalar: str = ""
    enum_class: str = ""
    message_class: str = ""
    gd_type: str = ""
    default_expr: str = ""
    packed: bool = False
    packed_codec: str = ""


@dataclass(frozen=True)
class ProtoParseResult:
    enums: tuple[ProtoEnum, ...]
    messages: tuple[ProtoMessage, ...]


def find_protoc(explicit: str = "") -> str:
    if explicit:
        return explicit
    env = os.environ.get("PROTOC", "").strip()
    if env:
        return env
    found = shutil.which("protoc")
    if not found:
        raise RuntimeError("protoc not found; install protobuf compiler or set PROTOC")
    return found


def message_to_snake(name: str) -> str:
    out: list[str] = []
    for i, ch in enumerate(name):
        if ch.isupper() and i > 0:
            prev = name[i - 1]
            nxt = name[i + 1] if i + 1 < len(name) else ""
            if prev.islower() or (nxt.islower() and nxt != ""):
                out.append("_")
        out.append(ch.lower())
    return "".join(out)


def _resolve_descriptor_name(proto_path: Path, includes: list[Path]) -> str:
    resolved = proto_path.resolve()
    for inc in includes:
        try:
            return resolved.relative_to(inc.resolve()).as_posix()
        except ValueError:
            continue
    return resolved.name


def _normalize_proto_path(name: str) -> str:
    return name.replace("\\", "/")


def _is_well_known_proto(name: str) -> bool:
    return _normalize_proto_path(name).startswith("google/protobuf/")


def _proto_scope(name: str) -> str:
    normalized = _normalize_proto_path(name)
    if normalized.startswith("game/"):
        return "game"
    if normalized.startswith("platform/"):
        return "platform"
    return "other"


def _should_emit_import(dep_name: str, *, target_name: str) -> bool:
    if _is_well_known_proto(dep_name):
        return False
    target_scope = _proto_scope(target_name)
    dep_scope = _proto_scope(dep_name)
    if target_scope == "game":
        return dep_scope == "game"
    if target_scope == "platform":
        return dep_scope == "platform"
    return True


def _collect_emit_file_names(
    files_by_name: dict[str, descriptor_pb2.FileDescriptorProto],
    target_name: str,
    *,
    emit_imports: bool,
) -> list[str]:
    target_name = _normalize_proto_path(target_name)
    if not emit_imports:
        return [target_name]

    ordered: list[str] = []
    seen: set[str] = set()

    def visit(name: str) -> None:
        name = _normalize_proto_path(name)
        if name in seen:
            return
        seen.add(name)
        file_desc = files_by_name.get(name)
        if file_desc is None:
            return
        for dep in file_desc.dependency:
            if _should_emit_import(dep, target_name=target_name):
                visit(dep)
        ordered.append(name)

    visit(target_name)
    return ordered


def _load_descriptor_set(
    proto_path: Path,
    *,
    protoc: str,
    include_paths: list[Path],
) -> tuple[descriptor_pb2.FileDescriptorSet, list[Path]]:
    proto_path = proto_path.resolve()
    if not proto_path.is_file():
        raise FileNotFoundError(proto_path)

    includes = [p.resolve() for p in include_paths]
    if proto_path.parent not in includes:
        includes.append(proto_path.parent)

    with tempfile.NamedTemporaryFile(suffix=".pb", delete=False) as tmp:
        desc_path = Path(tmp.name)

    try:
        cmd = [protoc, f"--descriptor_set_out={desc_path}", "--include_imports"]
        for inc in includes:
            cmd.append(f"-I{inc}")
        cmd.append(str(proto_path))

        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            detail = (result.stderr or result.stdout or "").strip()
            raise RuntimeError(f"protoc failed for {proto_path}:\n{detail}")

        fds = descriptor_pb2.FileDescriptorSet()
        fds.ParseFromString(desc_path.read_bytes())
    finally:
        desc_path.unlink(missing_ok=True)

    return fds, includes


def _file_desc_matches_proto(proto_path: Path, includes: list[Path], file_name: str) -> bool:
    return _normalize_proto_path(file_name) == _resolve_descriptor_name(proto_path, includes)


def _compound_name(path_parts: tuple[str, ...]) -> str:
    return "".join(path_parts)


def _register_keys(registry: dict[str, object], keys: list[str], value: object) -> None:
    for key in keys:
        if not key:
            continue
        registry[key] = value
        if not key.startswith("."):
            registry[f".{key}"] = value


def _build_type_registries(
    file_desc: descriptor_pb2.FileDescriptorProto,
    *,
    class_prefix: str,
) -> tuple[dict[str, ProtoEnum], dict[str, ProtoMessage], list[ProtoEnum], list[ProtoMessage]]:
    package = file_desc.package or ""
    enum_registry: dict[str, ProtoEnum] = {}
    message_registry: dict[str, ProtoMessage] = {}
    enums: list[ProtoEnum] = []
    messages: list[ProtoMessage] = []

    def enum_keys(parent_parts: tuple[str, ...], enum_name: str) -> list[str]:
        dotted = ".".join((*parent_parts, enum_name))
        keys = [enum_name, dotted]
        if package:
            keys.append(f"{package}.{dotted}")
        return keys

    def message_keys(parent_parts: tuple[str, ...], msg_name: str) -> list[str]:
        dotted = ".".join((*parent_parts, msg_name))
        keys = [msg_name, dotted]
        if package:
            keys.append(f"{package}.{dotted}")
        return keys

    def walk_enums(parent_parts: tuple[str, ...], enum_list) -> None:
        for enum in enum_list:
            compound = _compound_name((*parent_parts, enum.name))
            enum_def = ProtoEnum(
                name=enum.name,
                class_name=f"{class_prefix}{compound}",
                file_stem=message_to_snake(compound),
                values=tuple((v.name, v.number) for v in enum.value),
                source_file=file_desc.name,
            )
            enums.append(enum_def)
            _register_keys(enum_registry, enum_keys(parent_parts, enum.name), enum_def)

    def walk_messages(parent_parts: tuple[str, ...], msg_list) -> None:
        for msg in msg_list:
            path = (*parent_parts, msg.name)
            compound = _compound_name(path)
            msg_def = ProtoMessage(
                name=msg.name,
                class_name=f"{class_prefix}{compound}",
                file_stem=message_to_snake(compound),
                fields=(),
                source_file=file_desc.name,
            )
            messages.append(msg_def)
            _register_keys(message_registry, message_keys(parent_parts, msg.name), msg_def)

            walk_enums(path, msg.enum_type)
            walk_messages(path, msg.nested_type)

    walk_enums((), file_desc.enum_type)
    walk_messages((), file_desc.message_type)

    return enum_registry, message_registry, enums, messages


def _merge_type_registries(
    file_descs: list[descriptor_pb2.FileDescriptorProto],
    *,
    class_prefix: str,
) -> tuple[dict[str, ProtoEnum], dict[str, ProtoMessage]]:
    enum_registry: dict[str, ProtoEnum] = {}
    message_registry: dict[str, ProtoMessage] = {}
    class_name_files: dict[str, str] = {}

    for file_desc in file_descs:
        er, mr, _, _ = _build_type_registries(file_desc, class_prefix=class_prefix)
        for enum_def in er.values():
            prev = class_name_files.get(enum_def.class_name)
            if prev is not None and prev != file_desc.name:
                raise ValueError(
                    f"class_name collision {enum_def.class_name!r}: "
                    f"{file_desc.name} vs {prev}"
                )
            class_name_files[enum_def.class_name] = file_desc.name
        for msg_def in mr.values():
            prev = class_name_files.get(msg_def.class_name)
            if prev is not None and prev != file_desc.name:
                raise ValueError(
                    f"class_name collision {msg_def.class_name!r}: "
                    f"{file_desc.name} vs {prev}"
                )
            class_name_files[msg_def.class_name] = file_desc.name
        enum_registry.update(er)
        message_registry.update(mr)

    return enum_registry, message_registry


def _lookup(registry: dict[str, object], type_name: str) -> object | None:
    if type_name in registry:
        return registry[type_name]
    stripped = type_name.lstrip(".")
    if stripped in registry:
        return registry[stripped]
    for key, value in registry.items():
        if key.lstrip(".").endswith(stripped):
            return value
    return None


def _is_packed_repeated(field: descriptor_pb2.FieldDescriptorProto, *, syntax: str) -> bool:
    if field.label != descriptor_pb2.FieldDescriptorProto.LABEL_REPEATED:
        return False
    if field.type in NON_PACKABLE_FIELD_TYPES:
        return False
    if field.options.HasField("packed"):
        return field.options.packed
    return syntax == "proto3"


def _packed_codec_for_field(
    field: descriptor_pb2.FieldDescriptorProto,
    *,
    scalar: str = "",
) -> str:
    if field.type == descriptor_pb2.FieldDescriptorProto.TYPE_ENUM:
        return "varint"
    if scalar:
        return TYPE_INFO[scalar]["packed_codec"]
    return "varint"


def _resolve_field(
    field: descriptor_pb2.FieldDescriptorProto,
    *,
    context: str,
    syntax: str,
    enum_registry: dict[str, ProtoEnum],
    message_registry: dict[str, ProtoMessage],
) -> ProtoField:
    repeated = field.label == descriptor_pb2.FieldDescriptorProto.LABEL_REPEATED
    packed = _is_packed_repeated(field, syntax=syntax) if repeated else False

    if field.type == descriptor_pb2.FieldDescriptorProto.TYPE_MESSAGE:
        msg_def = _lookup(message_registry, field.type_name)
        if msg_def is None:
            raise ValueError(f"{context}: unknown message type {field.type_name!r}")
        gd_type = f"Array[{msg_def.class_name}]" if repeated else msg_def.class_name
        default = "[]" if repeated else "null"
        return ProtoField(
            name=field.name,
            number=field.number,
            repeated=repeated,
            kind="message",
            message_class=msg_def.class_name,
            gd_type=gd_type,
            default_expr=default,
            packed=False,
        )

    if field.type == descriptor_pb2.FieldDescriptorProto.TYPE_ENUM:
        enum_def = _lookup(enum_registry, field.type_name)
        if enum_def is None:
            raise ValueError(f"{context}: unknown enum type {field.type_name!r}")
        gd_type = "Array[int]" if repeated else "int"
        default = "[]" if repeated else "0"
        packed_codec = _packed_codec_for_field(field) if packed else ""
        return ProtoField(
            name=field.name,
            number=field.number,
            repeated=repeated,
            kind="enum",
            enum_class=enum_def.class_name,
            gd_type=gd_type,
            default_expr=default,
            packed=packed,
            packed_codec=packed_codec,
        )

    scalar = SCALAR_TYPE_MAP.get(field.type)
    if scalar is None:
        raise ValueError(f"{context}: unsupported field type {field.type} for {field.name!r}")

    info = TYPE_INFO[scalar]
    gd_type = info["repeated_gd_type"] if repeated else info["gd_type"]
    default = info["repeated_default"] if repeated else info["default"]
    packed_codec = info["packed_codec"] if packed else ""
    return ProtoField(
        name=field.name,
        number=field.number,
        repeated=repeated,
        kind="scalar",
        scalar=scalar,
        gd_type=gd_type,
        default_expr=default,
        packed=packed,
        packed_codec=packed_codec,
    )


def _fill_message_fields(
    file_desc: descriptor_pb2.FileDescriptorProto,
    *,
    class_prefix: str,
    enum_registry: dict[str, ProtoEnum],
    message_registry: dict[str, ProtoMessage],
    messages: list[ProtoMessage],
) -> list[ProtoMessage]:
    by_compound = {m.class_name.removeprefix(class_prefix): m for m in messages}
    filled: list[ProtoMessage] = []

    def walk_messages(parent_parts: tuple[str, ...], msg_list) -> None:
        for msg in msg_list:
            compound = _compound_name((*parent_parts, msg.name))
            ctx = f"{file_desc.name}:{'.'.join((*parent_parts, msg.name))}"
            fields = tuple(
                _resolve_field(
                    field,
                    context=ctx,
                    syntax=file_desc.syntax or "",
                    enum_registry=enum_registry,
                    message_registry=message_registry,
                )
                for field in msg.field
            )
            old = by_compound[compound]
            filled.append(
                ProtoMessage(
                    name=old.name,
                    class_name=old.class_name,
                    file_stem=old.file_stem,
                    fields=fields,
                    source_file=old.source_file,
                )
            )
            walk_messages((*parent_parts, msg.name), msg.nested_type)

    walk_messages((), file_desc.message_type)
    return filled


def parse_proto_messages(
    proto_path: Path,
    *,
    protoc: str,
    include_paths: list[Path],
    class_prefix: str = "Beast",
    emit_imports: bool = True,
) -> ProtoParseResult:
    fds, includes = _load_descriptor_set(proto_path, protoc=protoc, include_paths=include_paths)
    target_name = _resolve_descriptor_name(proto_path.resolve(), includes)
    files_by_name = {_normalize_proto_path(f.name): f for f in fds.file}

    if target_name not in files_by_name:
        raise RuntimeError(f"target proto missing from descriptor set: {target_name}")

    emit_names = _collect_emit_file_names(files_by_name, target_name, emit_imports=emit_imports)
    lookup_files = [
        file_desc
        for name, file_desc in files_by_name.items()
        if not _is_well_known_proto(name)
    ]
    enum_registry, message_registry = _merge_type_registries(
        lookup_files, class_prefix=class_prefix
    )

    all_enums: list[ProtoEnum] = []
    all_messages: list[ProtoMessage] = []
    seen_enum_stems: set[str] = set()
    seen_message_stems: set[str] = set()

    for name in emit_names:
        file_desc = files_by_name.get(_normalize_proto_path(name))
        if file_desc is None:
            continue

        _, _, enums, messages = _build_type_registries(file_desc, class_prefix=class_prefix)
        filled = _fill_message_fields(
            file_desc,
            class_prefix=class_prefix,
            enum_registry=enum_registry,
            message_registry=message_registry,
            messages=messages,
        )

        for enum_def in enums:
            if enum_def.file_stem in seen_enum_stems:
                continue
            seen_enum_stems.add(enum_def.file_stem)
            all_enums.append(enum_def)

        for message in filled:
            if message.file_stem in seen_message_stems:
                continue
            seen_message_stems.add(message.file_stem)
            all_messages.append(message)

    return ProtoParseResult(enums=tuple(all_enums), messages=tuple(all_messages))


def _emit_decode_field(field: ProtoField, indent: str) -> list[str]:
    lines: list[str] = []

    if field.kind == "message":
        lines.extend(
            [
                f"{indent}var chunk := _WireCodec.decode_length_delimited(data, offset)",
                f"{indent}if not chunk.ok:",
                f"{indent}\treturn null",
                f"{indent}var sub: {field.message_class} = {field.message_class}.from_bytes(chunk.value)",
                f"{indent}if sub == null:",
                f"{indent}\treturn null",
            ]
        )
        if field.repeated:
            lines.append(f"{indent}obj.{field.name}.append(sub)")
        else:
            lines.append(f"{indent}obj.{field.name} = sub")
        lines.append(f"{indent}offset = chunk.next_offset")
        return lines

    decode_call = ""
    if field.kind == "scalar":
        decode_call = TYPE_INFO[field.scalar]["decode_call"]
    elif field.kind == "enum":
        decode_call = "_WireCodec.decode_enum_field(data, offset, key.wire_type)"

    if field.repeated and field.packed:
        _, decode_fn = PACKED_WIRECODE[field.packed_codec]
        lines.extend(
            [
                f"{indent}if key.wire_type == _WireCodec.WireType.LENGTH_DELIMITED:",
                f"{indent}\tvar chunk := _WireCodec.decode_length_delimited(data, offset)",
                f"{indent}\tif not chunk.ok:",
                f"{indent}\t\treturn null",
                f"{indent}\tvar packed := _WireCodec.{decode_fn}(chunk.value)",
                f"{indent}\tif not packed.ok:",
                f"{indent}\t\treturn null",
                f"{indent}\tfor item in packed.values:",
                f"{indent}\t\tobj.{field.name}.append(item)",
                f"{indent}\toffset = chunk.next_offset",
                f"{indent}else:",
                f"{indent}\tvar parsed := {decode_call}",
                f"{indent}\tif not parsed.ok:",
                f"{indent}\t\treturn null",
                f"{indent}\tobj.{field.name}.append(parsed.value)",
                f"{indent}\toffset = parsed.next_offset",
            ]
        )
        return lines

    lines.extend(
        [
            f"{indent}var parsed := {decode_call}",
            f"{indent}if not parsed.ok:",
            f"{indent}\treturn null",
        ]
    )
    if field.repeated:
        lines.append(f"{indent}obj.{field.name}.append(parsed.value)")
    else:
        lines.append(f"{indent}obj.{field.name} = parsed.value")
    lines.append(f"{indent}offset = parsed.next_offset")
    return lines


def _emit_decode_match(fields: tuple[ProtoField, ...]) -> list[str]:
    if not fields:
        return [
            "\t\t\t_:",
            "\t\t\t\tvar skipped := _WireCodec.skip_field(data, offset, key.wire_type)",
            "\t\t\t\tif not skipped.ok:",
            "\t\t\t\t\treturn null",
            "\t\t\t\toffset = skipped.next_offset",
        ]

    lines: list[str] = []
    for field in sorted(fields, key=lambda f: f.number):
        lines.append(f"\t\t\t{field.number}:")
        lines.extend(_emit_decode_field(field, indent="\t\t\t\t"))

    lines.extend(
        [
            "\t\t\t_:",
            "\t\t\t\tvar skipped := _WireCodec.skip_field(data, offset, key.wire_type)",
            "\t\t\t\tif not skipped.ok:",
            "\t\t\t\t\treturn null",
            "\t\t\t\toffset = skipped.next_offset",
        ]
    )
    return lines


def _emit_encode_field(field: ProtoField) -> list[str]:
    lines: list[str] = []

    if field.repeated:
        if field.packed:
            encode_fn, _ = PACKED_WIRECODE[field.packed_codec]
            lines.append(f"\tif not {field.name}.is_empty():")
            lines.append(
                f"\t\tout.append_array(_WireCodec.{encode_fn}({field.number}, {field.name}))"
            )
            return lines

        if field.kind == "message":
            body = [
                "\tfor item in {name}:",
                "\t\tif item != null:",
                "\t\t\tout.append_array(_WireCodec.encode_bytes_field({num}, item.to_bytes()))",
            ]
        elif field.kind == "enum":
            body = [
                "\tfor item in {name}:",
                "\t\tif item != 0:",
                "\t\t\tout.append_array(_WireCodec.encode_enum_field({num}, item))",
            ]
        else:
            info = TYPE_INFO[field.scalar]
            encode_one = info["encode_call"].format(num=field.number, field="item")
            if field.scalar == "bool":
                body = [
                    "\tfor item in {name}:",
                    f"\t\tout.append_array({encode_one})",
                ]
            elif info["encode_empty_check"] is None:
                body = [
                    "\tfor item in {name}:",
                    f"\t\tout.append_array({encode_one})",
                ]
            else:
                body = [
                    "\tfor item in {name}:",
                    f"\t\tif {info['encode_empty_check'].format(field='item')}:",
                    f"\t\t\tout.append_array({encode_one})",
                ]
        wrapped = [line.format(name=field.name, num=field.number) for line in body]
        lines.append(f"\tif not {field.name}.is_empty():")
        lines.extend("\t" + line for line in wrapped)
        return lines

    if field.kind == "message":
        lines.append(f"\tif {field.name} != null:")
        lines.append(
            f"\t\tout.append_array(_WireCodec.encode_bytes_field({field.number}, {field.name}.to_bytes()))"
        )
        return lines

    if field.kind == "enum":
        lines.append(f"\tif {field.name} != 0:")
        lines.append(
            f"\t\tout.append_array(_WireCodec.encode_enum_field({field.number}, {field.name}))"
        )
        return lines

    info = TYPE_INFO[field.scalar]
    encode_call = info["encode_call"].format(num=field.number, field=field.name)
    empty_check = info["encode_empty_check"]
    if empty_check is None:
        lines.append(f"\tout.append_array({encode_call})")
    else:
        check = empty_check.format(field=field.name)
        lines.append(f"\tif {check}:")
        lines.append(f"\t\tout.append_array({encode_call})")
    return lines


def emit_enum_gdscript(
    enum_def: ProtoEnum,
    *,
    proto_path: Path,
) -> str:
    lines = [
        f"# Generated from {proto_path.as_posix()} — do not edit",
        f"class_name {enum_def.class_name}",
        "extends RefCounted",
        "",
    ]
    for name, number in enum_def.values:
        lines.append(f"const {name} = {number}")
    lines.append("")
    return "\n".join(lines)


def emit_message_gdscript(
    message: ProtoMessage,
    *,
    proto_path: Path,
    wire_codec_preload: str,
    load_res: str,
) -> str:
    var_lines = [f"var {field.name}: {field.gd_type} = {field.default_expr}" for field in message.fields]
    encode_lines: list[str] = []
    for field in message.fields:
        encode_lines.extend(_emit_encode_field(field))

    decode_match = _emit_decode_match(message.fields)
    needs_bytes_helper = any(f.kind == "scalar" and f.scalar == "bytes" for f in message.fields)

    lines = [
        f"# Generated from {proto_path.as_posix()} — do not edit",
        f"class_name {message.class_name}",
        "extends RefCounted",
        "",
        f'const _WireCodec := preload("{wire_codec_preload}")',
        "",
    ]
    if var_lines:
        lines.extend(var_lines)
    else:
        lines.append("pass")
    lines.extend(
        [
            "",
            "",
            "func to_bytes() -> PackedByteArray:",
            "\tvar out := PackedByteArray()",
        ]
    )
    if encode_lines:
        lines.extend(encode_lines)
    lines.extend(
        [
            "\treturn out",
            "",
            "",
            "static func from_bytes(data: PackedByteArray) -> {class_name}:".format(
                class_name=message.class_name
            ),
            "\tif data.is_empty():",
            "\t\treturn null",
            "",
            f'\tvar obj = load("{load_res}").new()',
            "\tvar offset: int = 0",
            "",
            "\twhile offset < data.size():",
            "\t\tvar key := _WireCodec.decode_field_key(data, offset)",
            "\t\tif not key.ok:",
            "\t\t\treturn null",
            "",
            "\t\toffset = key.next_offset",
            "\t\tmatch key.field_number:",
        ]
    )
    lines.extend(decode_match)
    lines.extend(["", "\treturn obj", ""])

    if needs_bytes_helper:
        lines.extend(
            [
                "",
                "",
                "static func _decode_bytes_field(data: PackedByteArray, offset: int, wire_type: int) -> Dictionary:",
                "\tif wire_type != _WireCodec.WireType.LENGTH_DELIMITED:",
                '\t\treturn {"ok": false}',
                "\tvar chunk := _WireCodec.decode_length_delimited(data, offset)",
                "\tif not chunk.ok:",
                '\t\treturn {"ok": false}',
                "\treturn {",
                '\t\t"ok": true,',
                '\t\t"value": chunk.value,',
                '\t\t"next_offset": chunk.next_offset,',
                "\t}",
            ]
        )

    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate Godot WireCodec message classes from proto3")
    parser.add_argument("--proto", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--wire-codec-preload", required=True)
    parser.add_argument("--load-prefix", required=True)
    parser.add_argument("--class-prefix", default="Beast")
    parser.add_argument("--protoc", default="", help="protoc binary (default: PROTOC env or PATH)")
    parser.add_argument(
        "--proto-include",
        action="append",
        default=[],
        help="Extra -I include path (repeatable)",
    )
    parser.add_argument(
        "--no-emit-imports",
        action="store_true",
        help="Only emit messages/enums from --proto itself (legacy behavior)",
    )
    args = parser.parse_args()

    if not args.proto.is_file():
        print(f"proto not found: {args.proto}", file=sys.stderr)
        return 1

    try:
        protoc = find_protoc(args.protoc)
        includes = [Path(p) for p in args.proto_include]
        parsed = parse_proto_messages(
            args.proto,
            protoc=protoc,
            include_paths=includes,
            class_prefix=args.class_prefix,
            emit_imports=not args.no_emit_imports,
        )
    except (RuntimeError, ValueError, FileNotFoundError) as exc:
        print(str(exc), file=sys.stderr)
        return 1

    if not parsed.enums and not parsed.messages:
        print(f"SKIP: no messages in {args.proto}")
        return 0

    load_prefix = args.load_prefix if args.load_prefix.endswith("/") else args.load_prefix + "/"
    args.out_dir.mkdir(parents=True, exist_ok=True)

    written = 0
    for enum_def in parsed.enums:
        out_path = args.out_dir / f"{enum_def.file_stem}.gd"
        source = enum_def.source_file or args.proto.as_posix()
        gd = emit_enum_gdscript(enum_def, proto_path=Path(source))
        out_path.write_text(gd, encoding="utf-8")
        print(f"Wrote {out_path}")
        written += 1

    for message in parsed.messages:
        out_path = args.out_dir / f"{message.file_stem}.gd"
        load_res = f"{load_prefix}{message.file_stem}.gd"
        source = message.source_file or args.proto.as_posix()
        gd = emit_message_gdscript(
            message,
            proto_path=Path(source),
            wire_codec_preload=args.wire_codec_preload,
            load_res=load_res,
        )
        out_path.write_text(gd, encoding="utf-8")
        print(f"Wrote {out_path}")
        written += 1

    print(
        f"Generated {len(parsed.enums)} enum(s), {len(parsed.messages)} message(s) "
        f"from {args.proto.name} via protoc"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
