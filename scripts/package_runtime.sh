#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${AICAM_BUILD_DIR:-$ROOT_DIR/build-aicam-check}"
DIST_DIR="${AICAM_DIST_DIR:-$ROOT_DIR/dist-aicam}"
SDK_ROOT="${SDK_ROOT:-}"
RKNN_RT_LIB="${AICAM_RKNNRT_LIB:-}"
RKAIQ_LIB="${AICAM_RKAIQ_LIB:-}"
RGA_LIB="${AICAM_RGA_LIB:-}"

if [[ -z "$RKNN_RT_LIB" && -n "$SDK_ROOT" ]]; then
  RKNN_RT_LIB="$SDK_ROOT/output/out/media_out/lib/librknnrt.so"
fi
if [[ -z "$RKAIQ_LIB" && -n "$SDK_ROOT" ]]; then
  RKAIQ_LIB="$SDK_ROOT/output/out/media_out/lib/librkaiq.so"
fi
if [[ -z "$RGA_LIB" && -n "$SDK_ROOT" ]]; then
  RGA_LIB="$SDK_ROOT/output/out/media_out/lib/librga.so"
fi
DEFAULT_MODEL_CANDIDATES=(
  "$ROOT_DIR/models/yolov5n_fp.rknn"
  "$ROOT_DIR/models/yolov5n.rknn"
)
DEFAULT_LABEL_CANDIDATES=(
  "$ROOT_DIR/models/coco_80_labels.txt"
)

CAMERA_BIN="$BUILD_DIR/camera/camera"
GATEWAY_BIN="$BUILD_DIR/camera_gateway/camera_gateway"

if [[ ! -x "$CAMERA_BIN" ]]; then
  echo "Camera binary not found: $CAMERA_BIN" >&2
  exit 1
fi

if [[ ! -x "$GATEWAY_BIN" ]]; then
  echo "Gateway binary not found: $GATEWAY_BIN" >&2
  exit 1
fi

mkdir -p "$DIST_DIR" "$DIST_DIR/models" "$DIST_DIR/logs" "$DIST_DIR/config"

if [[ -d "$ROOT_DIR/dist-aura-sdk" ]]; then
  cp -a "$ROOT_DIR/dist-aura-sdk/." "$DIST_DIR/"
fi

cp -f "$CAMERA_BIN" "$DIST_DIR/camera"
cp -f "$GATEWAY_BIN" "$DIST_DIR/camera_gateway"
cp -f "$ROOT_DIR/config/aicam.env.example" "$DIST_DIR/config/aicam.env.example"
cp -f "$ROOT_DIR/scripts/start_aicam_board.sh" "$DIST_DIR/start_aicam.sh"

if [[ -n "$RKNN_RT_LIB" && -f "$RKNN_RT_LIB" ]]; then
  cp -f "$RKNN_RT_LIB" "$DIST_DIR/librknnrt.so"
else
  echo "Warning: RKNN runtime library not found. Set AICAM_RKNNRT_LIB or SDK_ROOT before packaging." >&2
fi

if [[ -n "$RKAIQ_LIB" && -f "$RKAIQ_LIB" ]]; then
  cp -f "$RKAIQ_LIB" "$DIST_DIR/librkaiq.so"
else
  echo "Warning: RKAIQ runtime library not found. Set AICAM_RKAIQ_LIB or SDK_ROOT before packaging." >&2
fi

if [[ -n "$RGA_LIB" && -f "$RGA_LIB" ]]; then
  cp -f "$RGA_LIB" "$DIST_DIR/librga.so"
else
  echo "Warning: RGA runtime library not found. Set AICAM_RGA_LIB or SDK_ROOT before packaging." >&2
fi

for model_path in "${DEFAULT_MODEL_CANDIDATES[@]}"; do
  if [[ -f "$model_path" ]]; then
    cp -f "$model_path" "$DIST_DIR/models/"
    break
  fi
done

for label_path in "${DEFAULT_LABEL_CANDIDATES[@]}"; do
  if [[ -f "$label_path" ]]; then
    cp -f "$label_path" "$DIST_DIR/models/"
    break
  fi
done

chmod +x "$DIST_DIR/start_aicam.sh"

echo "Runtime package prepared at: $DIST_DIR"
