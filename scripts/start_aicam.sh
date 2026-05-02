#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_FILE="${AICAM_ENV_FILE:-$ROOT_DIR/config/aicam.env.example}"
BUILD_DIR="${AICAM_BUILD_DIR:-$ROOT_DIR/build-aicam-check}"
GATEWAY_BIN="${AICAM_GATEWAY_BIN:-$BUILD_DIR/camera_gateway/camera_gateway}"
CAMERA_BIN="${AICAM_CAMERA_BIN:-$BUILD_DIR/camera/camera}"

if [[ -f "$ENV_FILE" ]]; then
  # shellcheck disable=SC1090
  source "$ENV_FILE"
fi

mkdir -p "$ROOT_DIR/logs"

echo "Using build directory: $BUILD_DIR"
echo "Gateway binary: $GATEWAY_BIN"
echo "Camera binary: $CAMERA_BIN"

if [[ ! -x "$GATEWAY_BIN" ]]; then
  echo "Gateway binary not found or not executable: $GATEWAY_BIN" >&2
  exit 1
fi

if [[ ! -x "$CAMERA_BIN" ]]; then
  echo "Camera binary not found or not executable: $CAMERA_BIN" >&2
  exit 1
fi

"$GATEWAY_BIN" >"$ROOT_DIR/logs/gateway.out" 2>&1 &
GATEWAY_PID=$!

cleanup() {
  kill "$GATEWAY_PID" "${CAMERA_PID:-}" 2>/dev/null || true
}

trap cleanup EXIT INT TERM

sleep 1
"$CAMERA_BIN"
