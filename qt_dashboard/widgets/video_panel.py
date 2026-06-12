from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtGui import QImage, QPixmap
from PySide6.QtWidgets import QLabel, QVBoxLayout, QWidget


class VideoPanel(QWidget):
    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._image: QImage | None = None

        self.label = QLabel("No video")
        self.label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.label.setMinimumSize(640, 360)
        self.label.setStyleSheet(
            "QLabel { background: #111827; color: #cbd5e1; border: 1px solid #263244; }"
        )

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self.label)

    def set_frame(self, image: QImage) -> None:
        self._image = image
        self._render()

    def clear(self) -> None:
        self._image = None
        self.label.setText("No video")
        self.label.setPixmap(QPixmap())

    def resizeEvent(self, event) -> None:  # noqa: N802
        super().resizeEvent(event)
        self._render()

    def _render(self) -> None:
        if self._image is None:
            return
        pixmap = QPixmap.fromImage(self._image)
        scaled = pixmap.scaled(
            self.label.size(),
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.SmoothTransformation,
        )
        self.label.setPixmap(scaled)
