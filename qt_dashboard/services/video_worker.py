from __future__ import annotations

import os
import time

from PySide6.QtCore import QThread, Signal
from PySide6.QtGui import QImage


class VideoWorker(QThread):
    frame_ready = Signal(QImage)
    status_changed = Signal(str)

    def __init__(self, url: str, parent=None) -> None:
        super().__init__(parent)
        self.url = url
        self._running = False

    def stop(self) -> None:
        self._running = False
        if not self.wait(1500):
            self.terminate()
            self.wait(1000)

    def run(self) -> None:
        try:
            import cv2
        except ImportError:
            self.status_changed.emit("未安装 opencv-python")
            return

        self._running = True
        self.status_changed.emit(f"正在打开视频流：{self.url}")
        os.environ.setdefault("OPENCV_FFMPEG_CAPTURE_OPTIONS", "stimeout;3000000|rw_timeout;3000000")
        capture = None
        open_attempts = 0
        while self._running:
            capture = cv2.VideoCapture()
            if hasattr(cv2, "CAP_PROP_OPEN_TIMEOUT_MSEC"):
                capture.set(cv2.CAP_PROP_OPEN_TIMEOUT_MSEC, 3000)
            if hasattr(cv2, "CAP_PROP_READ_TIMEOUT_MSEC"):
                capture.set(cv2.CAP_PROP_READ_TIMEOUT_MSEC, 3000)

            try:
                opened = capture.open(self.url, cv2.CAP_FFMPEG)
            except Exception:  # noqa: BLE001
                opened = False

            if opened and capture.isOpened():
                break
            capture.release()
            open_attempts += 1
            if open_attempts == 1 or open_attempts % 10 == 0:
                self.status_changed.emit("等待视频端点响应")
            time.sleep(0.5)

        if not self._running or capture is None:
            return

        self.status_changed.emit("视频流已连接")
        failed_reads = 0
        while self._running:
            ok, frame = capture.read()
            if not ok or frame is None:
                failed_reads += 1
                if failed_reads >= 30:
                    self.status_changed.emit("等待视频帧")
                    failed_reads = 0
                time.sleep(0.05)
                continue

            failed_reads = 0
            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            height, width, channels = rgb.shape
            bytes_per_line = channels * width
            image = QImage(
                rgb.data,
                width,
                height,
                bytes_per_line,
                QImage.Format.Format_RGB888,
            ).copy()
            self.frame_ready.emit(image)

        capture.release()
        self.status_changed.emit("视频流已停止")
