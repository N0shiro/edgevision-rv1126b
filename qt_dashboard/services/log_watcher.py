from __future__ import annotations

from pathlib import Path
from typing import Optional

from PySide6.QtCore import QThread, Signal

from .adb_manager import AdbManager
from .jsonl_reader import (
    DetectionEvent,
    JsonlTail,
    MetricsSample,
    flatten_detection_events,
    parse_metrics_samples,
)


class LogWatcher(QThread):
    events_ready = Signal(list)
    metrics_ready = Signal(list)
    status_changed = Signal(str)

    def __init__(
        self,
        events_path: Path,
        metrics_path: Path,
        poll_interval_ms: int = 2000,
        use_adb_pull: bool = False,
        adb_manager: Optional[AdbManager] = None,
        adb_serial: Optional[str] = None,
        board_log_dir: str = "/userdata/aicam/logs",
        parent=None,
    ) -> None:
        super().__init__(parent)
        self.events_path = Path(events_path)
        self.metrics_path = Path(metrics_path)
        self.poll_interval_ms = max(500, poll_interval_ms)
        self.use_adb_pull = use_adb_pull
        self.adb_manager = adb_manager or AdbManager()
        self.adb_serial = adb_serial
        self.board_log_dir = board_log_dir
        self._running = False

        self.events_tail = JsonlTail(self.events_path)
        self.metrics_tail = JsonlTail(self.metrics_path)

    def stop(self) -> None:
        self._running = False
        self.wait(1500)

    def run(self) -> None:
        self._running = True
        self.status_changed.emit("log watcher started")

        while self._running:
            if self.use_adb_pull:
                try:
                    self.adb_manager.pull_logs(
                        self.board_log_dir,
                        self.events_path.parent,
                        serial=self.adb_serial,
                    )
                except RuntimeError as exc:
                    self.status_changed.emit(str(exc))

            event_items = self.events_tail.read_new_objects()
            metric_items = self.metrics_tail.read_new_objects()

            events = flatten_detection_events(event_items)
            metrics = parse_metrics_samples(metric_items)
            if events:
                self.events_ready.emit(events)
            if metrics:
                self.metrics_ready.emit(metrics)

            self.msleep(self.poll_interval_ms)

        self.status_changed.emit("log watcher stopped")
