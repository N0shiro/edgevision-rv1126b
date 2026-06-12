from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, Iterable, List


@dataclass
class DetectionEvent:
    timestamp: str
    frame_sequence: int
    label: str
    class_id: int
    score: float
    x1: float
    y1: float
    x2: float
    y2: float
    inference_ms: float
    backend: str


@dataclass
class MetricsSample:
    timestamp: str
    capture_fps: float
    encode_fps: float
    ai_fps: float
    average_encode_ms: float
    average_inference_ms: float
    cpu_percent: float
    rss_mb: float
    bytes_sent: int
    encode_queue_drops: int
    ai_queue_drops: int


class JsonlTail:
    def __init__(self, path: Path) -> None:
        self.path = Path(path)
        self.offset = 0

    def reset(self) -> None:
        self.offset = 0

    def read_new_objects(self) -> List[Dict[str, Any]]:
        if not self.path.exists():
            return []

        size = self.path.stat().st_size
        if size < self.offset:
            self.offset = 0

        objects: List[Dict[str, Any]] = []
        with self.path.open("r", encoding="utf-8", errors="replace") as handle:
            handle.seek(self.offset)
            for line in handle:
                line = line.strip()
                if not line:
                    continue
                try:
                    value = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if isinstance(value, dict):
                    objects.append(value)
            self.offset = handle.tell()
        return objects


def flatten_detection_events(items: Iterable[Dict[str, Any]]) -> List[DetectionEvent]:
    records: List[DetectionEvent] = []
    for item in items:
        detections = item.get("detections") or []
        if not isinstance(detections, list):
            continue

        for detection in detections:
            if not isinstance(detection, dict):
                continue
            records.append(
                DetectionEvent(
                    timestamp=str(item.get("timestamp", "")),
                    frame_sequence=int(item.get("frame_sequence", 0) or 0),
                    label=str(detection.get("label", "")),
                    class_id=int(detection.get("class_id", -1) or -1),
                    score=float(detection.get("score", 0.0) or 0.0),
                    x1=float(detection.get("x1", 0.0) or 0.0),
                    y1=float(detection.get("y1", 0.0) or 0.0),
                    x2=float(detection.get("x2", 0.0) or 0.0),
                    y2=float(detection.get("y2", 0.0) or 0.0),
                    inference_ms=float(item.get("inference_ms", 0.0) or 0.0),
                    backend=str(item.get("backend", "")),
                )
            )
    return records


def parse_metrics_samples(items: Iterable[Dict[str, Any]]) -> List[MetricsSample]:
    samples: List[MetricsSample] = []
    for item in items:
        samples.append(
            MetricsSample(
                timestamp=str(item.get("timestamp", "")),
                capture_fps=float(item.get("capture_fps", 0.0) or 0.0),
                encode_fps=float(item.get("encode_fps", 0.0) or 0.0),
                ai_fps=float(item.get("ai_fps", 0.0) or 0.0),
                average_encode_ms=float(item.get("average_encode_ms", 0.0) or 0.0),
                average_inference_ms=float(item.get("average_inference_ms", 0.0) or 0.0),
                cpu_percent=float(item.get("cpu_percent", 0.0) or 0.0),
                rss_mb=float(item.get("rss_mb", 0.0) or 0.0),
                bytes_sent=int(item.get("bytes_sent", 0) or 0),
                encode_queue_drops=int(item.get("encode_queue_drops", 0) or 0),
                ai_queue_drops=int(item.get("ai_queue_drops", 0) or 0),
            )
        )
    return samples
