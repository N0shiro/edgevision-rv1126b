from __future__ import annotations

import csv
from pathlib import Path
from typing import Iterable

from .jsonl_reader import DetectionEvent


def export_detection_events(path: Path, events: Iterable[DetectionEvent]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "timestamp",
                "frame_sequence",
                "label",
                "class_id",
                "score",
                "x1",
                "y1",
                "x2",
                "y2",
                "inference_ms",
                "backend",
            ]
        )
        for event in events:
            writer.writerow(
                [
                    event.timestamp,
                    event.frame_sequence,
                    event.label,
                    event.class_id,
                    f"{event.score:.4f}",
                    f"{event.x1:.1f}",
                    f"{event.y1:.1f}",
                    f"{event.x2:.1f}",
                    f"{event.y2:.1f}",
                    f"{event.inference_ms:.2f}",
                    event.backend,
                ]
            )
