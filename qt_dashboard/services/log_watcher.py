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
        self.use_adb_pull = use_adb_pull
        if self.use_adb_pull and poll_interval_ms == 2000:
            poll_interval_ms = 5000
        self.poll_interval_ms = max(500, poll_interval_ms)
        self.adb_manager = adb_manager or AdbManager()
        self.adb_serial = adb_serial
        self.board_log_dir = board_log_dir
        self._running = False
        self.remote_events_offset = 0
        self.remote_metrics_offset = 0
        self.pull_failures = 0

        self.events_tail = JsonlTail(self.events_path)
        self.metrics_tail = JsonlTail(self.metrics_path)

    def stop(self) -> None:
        self._running = False
        if not self.wait(9000):
            self.status_changed.emit("日志监听停止超时，等待当前 ADB 命令结束")

    def run(self) -> None:
        self._running = True
        self.status_changed.emit("日志监听已启动")
        if self.use_adb_pull:
            self._prepare_adb_tail()

        while self._running:
            if self.use_adb_pull:
                try:
                    self._sync_adb_tail()
                except RuntimeError as exc:
                    self.pull_failures += 1
                    if self.pull_failures == 1 or self.pull_failures % 5 == 0:
                        self.status_changed.emit(f"日志增量同步失败：{exc}")
                else:
                    if self.pull_failures:
                        self.status_changed.emit("日志增量同步已恢复")
                    self.pull_failures = 0

            event_items = self.events_tail.read_new_objects()
            metric_items = self.metrics_tail.read_new_objects()

            events = flatten_detection_events(event_items)
            metrics = parse_metrics_samples(metric_items)
            if events:
                self.events_ready.emit(events)
            if metrics:
                self.metrics_ready.emit(metrics)

            self.msleep(self.poll_interval_ms)

        self.status_changed.emit("日志监听已停止")

    def _prepare_adb_tail(self) -> None:
        self.events_path.parent.mkdir(parents=True, exist_ok=True)
        self.metrics_path.parent.mkdir(parents=True, exist_ok=True)
        self.events_path.write_text("", encoding="utf-8")
        self.metrics_path.write_text("", encoding="utf-8")
        self.events_tail.reset()
        self.metrics_tail.reset()

        events_remote = f"{self.board_log_dir.rstrip('/')}/events.jsonl"
        metrics_remote = f"{self.board_log_dir.rstrip('/')}/metrics.jsonl"
        try:
            self.remote_events_offset = max(
                0,
                self.adb_manager.remote_file_size(events_remote, serial=self.adb_serial) - 32768,
            )
            self.remote_metrics_offset = max(
                0,
                self.adb_manager.remote_file_size(metrics_remote, serial=self.adb_serial) - 32768,
            )
        except RuntimeError as exc:
            self.status_changed.emit(f"日志初始定位失败：{exc}")
            self.remote_events_offset = 0
            self.remote_metrics_offset = 0

    def _sync_adb_tail(self) -> None:
        events_remote = f"{self.board_log_dir.rstrip('/')}/events.jsonl"
        metrics_remote = f"{self.board_log_dir.rstrip('/')}/metrics.jsonl"

        events_text, self.remote_events_offset = self.adb_manager.tail_file(
            events_remote,
            self.remote_events_offset,
            serial=self.adb_serial,
        )
        metrics_text, self.remote_metrics_offset = self.adb_manager.tail_file(
            metrics_remote,
            self.remote_metrics_offset,
            serial=self.adb_serial,
        )

        if events_text:
            self._append_text(self.events_path, events_text)
        if metrics_text:
            self._append_text(self.metrics_path, metrics_text)

    def _append_text(self, path: Path, text: str) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("a", encoding="utf-8", errors="replace") as handle:
            handle.write(text)
