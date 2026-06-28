#!/usr/bin/env python3
"""Unit tests for gen_messages_from_proto.py (protoc descriptor parser)."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from gen_messages_from_proto import (
    emit_message_gdscript,
    find_protoc,
    message_to_snake,
    parse_proto_messages,
)


class GenMessagesFromProtoTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.protoc = find_protoc()
        cls.repo = Path(__file__).resolve().parents[2]
        cls.proto_root = cls.repo / "bizconfig/protocol"

    def test_message_to_snake(self) -> None:
        self.assertEqual(message_to_snake("PingRequest2"), "ping_request2")
        self.assertEqual(message_to_snake("AuthRequest"), "auth_request")
        self.assertEqual(message_to_snake("PlayerPosition"), "player_position")

    def test_parse_demo_ping(self) -> None:
        proto = self.proto_root / "game/example/demo_event/demo_event.proto"
        parsed = parse_proto_messages(proto, protoc=self.protoc, include_paths=[self.proto_root])
        names = [m.name for m in parsed.messages]
        self.assertIn("PingRequest2", names)
        ping2 = next(m for m in parsed.messages if m.name == "PingRequest2")
        self.assertEqual(len(ping2.fields), 1)
        self.assertEqual(ping2.fields[0].name, "text")
        self.assertEqual(ping2.fields[0].scalar, "string")

    def test_emit_ping_request2_shape(self) -> None:
        proto = self.proto_root / "game/example/demo_event/demo_event.proto"
        parsed = parse_proto_messages(proto, protoc=self.protoc, include_paths=[self.proto_root])
        ping2 = next(m for m in parsed.messages if m.name == "PingRequest2")
        gd = emit_message_gdscript(
            ping2,
            proto_path=proto,
            wire_codec_preload="res://addons/beast_sdk/impl/codec/wire_codec.gd",
            load_res="res://infra/generated/protocol/demo_event/ping_request2.gd",
        )
        self.assertIn("class_name BeastPingRequest2", gd)
        self.assertIn("encode_string_field(1, text)", gd)
        self.assertIn('load("res://infra/generated/protocol/demo_event/ping_request2.gd")', gd)

    def test_parse_auth_response(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            proto = Path(tmp) / "auth.proto"
            proto.write_text(
                """
syntax = "proto3";
message AuthResponse {
  bool success = 1;
  string message = 2;
  uint64 pid = 3;
}
""",
                encoding="utf-8",
            )
            parsed = parse_proto_messages(proto, protoc=self.protoc, include_paths=[Path(tmp)])
            self.assertEqual(len(parsed.messages), 1)
            self.assertEqual(len(parsed.messages[0].fields), 3)

            gd = emit_message_gdscript(
                parsed.messages[0],
                proto_path=proto,
                wire_codec_preload="res://beast_sdk/impl/codec/wire_codec.gd",
                load_res="res://beast_sdk/generated/auth_response.gd",
            )
            self.assertIn("encode_bool_field(1, success)", gd)
            self.assertIn("encode_string_field(2, message)", gd)
            self.assertIn("encode_uint64_field(3, pid)", gd)

    def test_repeated_nested_enum_float(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            proto = Path(tmp) / "advanced.proto"
            proto.write_text(
                """
syntax = "proto3";
message Advanced {
  repeated string tags = 1;
  float ratio = 2;
  sint32 delta = 3;
  Status status = 4;
  repeated Position points = 5;
  message Position {
    fixed32 x = 1;
    fixed32 y = 2;
  }
  enum Status {
    STATUS_UNKNOWN = 0;
    STATUS_OK = 1;
  }
}
""",
                encoding="utf-8",
            )
            parsed = parse_proto_messages(proto, protoc=self.protoc, include_paths=[Path(tmp)])
            self.assertEqual(len(parsed.messages), 2)
            stems = {m.file_stem for m in parsed.messages}
            self.assertIn("advanced", stems)
            self.assertIn("advanced_position", stems)

            advanced = next(m for m in parsed.messages if m.file_stem == "advanced")
            by_name = {f.name: f for f in advanced.fields}
            self.assertTrue(by_name["tags"].repeated)
            self.assertEqual(by_name["ratio"].scalar, "float")
            self.assertEqual(by_name["delta"].scalar, "sint32")
            self.assertEqual(by_name["status"].kind, "enum")
            self.assertTrue(by_name["points"].repeated)
            self.assertEqual(by_name["points"].message_class, "BeastAdvancedPosition")

            self.assertEqual(len(parsed.enums), 1)
            self.assertEqual(parsed.enums[0].class_name, "BeastAdvancedStatus")

            gd = emit_message_gdscript(
                advanced,
                proto_path=proto,
                wire_codec_preload="res://beast_sdk/impl/codec/wire_codec.gd",
                load_res="res://beast_sdk/generated/advanced.gd",
            )
            self.assertIn("Array[String]", gd)
            self.assertIn("tags.append(parsed.value)", gd)
            self.assertIn("encode_float_field(2, ratio)", gd)
            self.assertIn("encode_sint32_field(3, delta)", gd)
            self.assertIn("BeastAdvancedPosition.from_bytes", gd)
            self.assertIn("points.append(sub)", gd)

    def test_packed_repeated_int32(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            proto = Path(tmp) / "packed.proto"
            proto.write_text(
                """
syntax = "proto3";
message PackedMsg {
  repeated int32 ids = 1;
  repeated string names = 2;
}
""",
                encoding="utf-8",
            )
            parsed = parse_proto_messages(proto, protoc=self.protoc, include_paths=[Path(tmp)])
            msg = parsed.messages[0]
            ids_field = next(f for f in msg.fields if f.name == "ids")
            names_field = next(f for f in msg.fields if f.name == "names")
            self.assertTrue(ids_field.packed)
            self.assertFalse(names_field.packed)

            gd = emit_message_gdscript(
                msg,
                proto_path=proto,
                wire_codec_preload="res://beast_sdk/impl/codec/wire_codec.gd",
                load_res="res://beast_sdk/generated/packed_msg.gd",
            )
            self.assertIn("encode_packed_varint_field(1, ids)", gd)
            self.assertIn("decode_packed_varint_chunk(chunk.value)", gd)
            self.assertIn("names.append(parsed.value)", gd)
            self.assertNotIn("encode_packed_varint_field(2, names)", gd)

    def test_repeated_message_encode_indent(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            proto = Path(tmp) / "rows.proto"
            proto.write_text(
                """
syntax = "proto3";
message Row {
  string name = 1;
}
message Table {
  repeated Row rows = 1;
}
""",
                encoding="utf-8",
            )
            parsed = parse_proto_messages(proto, protoc=self.protoc, include_paths=[Path(tmp)])
            table = next(m for m in parsed.messages if m.name == "Table")
            gd = emit_message_gdscript(
                table,
                proto_path=proto,
                wire_codec_preload="res://beast_sdk/impl/codec/wire_codec.gd",
                load_res="res://beast_sdk/generated/table.gd",
            )
            self.assertIn("\tif not rows.is_empty():\n\t\tfor item in rows:", gd)
            self.assertNotIn("\tif not rows.is_empty():\n\tfor item in rows:", gd)
            self.assertIn("var sub: BeastRow = BeastRow.from_bytes(chunk.value)", gd)
            self.assertIn("static func from_bytes(data: PackedByteArray) -> BeastTable:", gd)


if __name__ == "__main__":
    unittest.main()
