#!/usr/bin/env python3
"""Convert an ONNX detection model to RKNN for RV1126/RV1109-style targets."""

import argparse
import os
import sys
from typing import List


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Convert ONNX model to RKNN.")
    parser.add_argument("--onnx", required=True, help="Path to the ONNX model.")
    parser.add_argument("--output", required=True, help="Output RKNN file path.")
    parser.add_argument("--target", default="rv1126", help="Target platform, e.g. rv1126.")
    parser.add_argument("--dataset", default="", help="Dataset file for quantization.")
    parser.add_argument("--mean-values", default="0,0,0", help="Comma-separated mean values.")
    parser.add_argument("--std-values", default="255,255,255", help="Comma-separated std values.")
    parser.add_argument("--quantize", action="store_true", help="Enable quantization build.")
    parser.add_argument("--verbose", action="store_true", help="Enable RKNN verbose logging.")
    return parser.parse_args()


def parse_triplet(raw: str) -> List[float]:
    parts = [item.strip() for item in raw.split(",") if item.strip()]
    if len(parts) != 3:
        raise ValueError(f"Expected three comma-separated values, got: {raw}")
    return [float(item) for item in parts]


def main() -> int:
    args = parse_args()

    if not os.path.exists(args.onnx):
        print(f"ONNX file not found: {args.onnx}", file=sys.stderr)
        return 1

    output_dir = os.path.dirname(os.path.abspath(args.output))
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)

    try:
        from rknn.api import RKNN
    except ImportError as exc:
        print("RKNN Toolkit2 is not installed in the current Python environment.", file=sys.stderr)
        print(str(exc), file=sys.stderr)
        return 1

    mean_values = [parse_triplet(args.mean_values)]
    std_values = [parse_triplet(args.std_values)]

    rknn = RKNN(verbose=args.verbose)

    print("[1/4] Configuring RKNN environment...")
    rknn.config(
        target_platform=args.target,
        mean_values=mean_values,
        std_values=std_values,
    )

    print("[2/4] Loading ONNX model...")
    ret = rknn.load_onnx(model=args.onnx)
    if ret != 0:
        print(f"load_onnx failed: {ret}", file=sys.stderr)
        return ret

    print("[3/4] Building RKNN model...")
    ret = rknn.build(
        do_quantization=args.quantize,
        dataset=args.dataset if args.quantize else None,
    )
    if ret != 0:
        print(f"build failed: {ret}", file=sys.stderr)
        return ret

    print("[4/4] Exporting RKNN model...")
    ret = rknn.export_rknn(args.output)
    rknn.release()
    if ret != 0:
        print(f"export_rknn failed: {ret}", file=sys.stderr)
        return ret

    print(f"RKNN model exported to: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
