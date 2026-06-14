from __future__ import annotations

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
        self.wait(1500)

    def run(self) -> None:
        try:
            import cv2
        except ImportError:
            self.status_changed.emit("opencv-python is not installed")
            return

        self._running = True
        self.status_changed.emit(f"opening video: {self.url}")
        capture = None
        open_attempts = 0
        while self._running:
            capture = cv2.VideoCapture(self.url)
            if capture.isOpened():
                break
            capture.release()
            open_attempts += 1
            if open_attempts == 1 or open_attempts % 10 == 0:
                self.status_changed.emit("waiting for video endpoint")
            time.sleep(0.5)

        if not self._running or capture is None:
            return

        self.status_changed.emit("video connected")
        failed_reads = 0
        while self._running:
            ok, frame = capture.read()
            if not ok or frame is None:
                failed_reads += 1
                if failed_reads >= 30:
                    self.status_changed.emit("waiting for video frames")
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
        self.status_changed.emit("video stopped")
