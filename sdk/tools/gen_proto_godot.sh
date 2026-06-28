#!/usr/bin/env bash
# Beast SDK platform proto 生成（Linux / CI）：routes + message 编解码
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_ROOT="$(cd "${SDK_ROOT}/.." && pwd)"
PROTO_ROOT="${REPO_ROOT}/bizconfig/protocol"
OUT_DIR="${SDK_ROOT}/godot/beast_sdk/generated"
MANIFEST="${SCRIPT_DIR}/proto_manifest.json"
GEN_ROUTES="${SCRIPT_DIR}/gen_routes_from_proto.py"
GEN_MESSAGES="${SCRIPT_DIR}/gen_messages_from_proto.py"
WIRE_CODEC_PRELOAD="res://beast_sdk/impl/codec/wire_codec.gd"
LOAD_PREFIX="res://beast_sdk/generated/"

if [[ ! -d "${PROTO_ROOT}" ]]; then
  echo "Proto root not found: ${PROTO_ROOT}" >&2
  exit 1
fi

mkdir -p "${OUT_DIR}"

echo "Proto root : ${PROTO_ROOT}"
echo "Output dir : ${OUT_DIR}"
echo ""

python3 - <<'PY' "${MANIFEST}" "${PROTO_ROOT}" "${OUT_DIR}" "${GEN_ROUTES}" "${GEN_MESSAGES}" "${WIRE_CODEC_PRELOAD}" "${LOAD_PREFIX}"
import json, os, subprocess, sys

manifest_path, proto_root, out_dir, gen_routes, gen_messages, wire_codec, load_prefix = sys.argv[1:8]

with open(manifest_path, encoding="utf-8") as f:
    manifest = json.load(f)

def proto_path(entry_proto: str) -> str:
    return os.path.join(proto_root, entry_proto.replace("/", os.sep))

for entry in manifest.get("v1_client_tcp_routes", []):
    path = proto_path(entry["proto"])
    if not os.path.isfile(path):
        print(f"[MISSING routes] {entry['proto']}", file=sys.stderr)
        continue
    out = os.path.join(out_dir, entry["out"])
    subprocess.check_call([
        sys.executable, gen_routes,
        "--proto", path,
        "--out", out,
        "--class-name", entry["class_name"],
    ])
    print(f"[routes] {entry['out']}")

seen = set()
for entry in manifest.get("v1_client_tcp_messages", []):
    path = proto_path(entry["proto"])
    if not os.path.isfile(path):
        print(f"[MISSING messages] {entry['proto']}", file=sys.stderr)
        continue
    if path in seen:
        continue
    seen.add(path)
    print(f"[OK] {entry['proto']}")
    subprocess.check_call([
        sys.executable, gen_messages,
        "--proto", path,
        "--out-dir", out_dir,
        "--wire-codec-preload", wire_codec,
        "--load-prefix", load_prefix,
        "--proto-include", proto_root,
    ])

print("Done.")
PY
