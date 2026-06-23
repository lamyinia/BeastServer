#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"

OUT_ROOT="${REPO_ROOT}/beastserver/build/${BUILD_TYPE}/bizconfig"
RAW_DIR="${REPO_ROOT}/bizconfig/static-xlsx"
SCHEMA_DIR="${REPO_ROOT}/bizconfig/scheme"
EXPORT_BIN="${REPO_ROOT}/tools/biz_export/biz_export"

mkdir -p "${OUT_ROOT}/server" "${OUT_ROOT}/client"

if [[ ! -x "${EXPORT_BIN}" ]]; then
  echo "biz_export not built: ${EXPORT_BIN}" >&2
  echo "Build tools/biz_export first, then re-run this script." >&2
  exit 1
fi

exec "${EXPORT_BIN}" \
  --raw "${RAW_DIR}" \
  --schema "${SCHEMA_DIR}" \
  --server "${OUT_ROOT}/server" \
  --client "${OUT_ROOT}/client" \
  --manifest "${OUT_ROOT}/manifest.json"
