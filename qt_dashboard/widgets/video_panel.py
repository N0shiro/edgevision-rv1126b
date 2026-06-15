from __future__ import annotations

from PySide6.QtCore import QSize, Qt
from PySide6.QtGui import QImage, QPixmap
from PySide6.QtWidgets import QLabel, QSizePolicy, QVBoxLayout, QWidget


class VideoPanel(QWidget):
    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._image: QImage | None = None
        self._aspect_ratio = 16 / 9
        self.setMinimumSize(360, 220)
        self.setObjectName("videoPanel")
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)

        self.label = QLabel("等待视频流")
        self.label.setObjectName("videoViewport")
        self.label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.label.setMinimumSize(360, 203)
        self.label.setFixedSize(640, 360)
        self.label.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Fixed)
        self.label.setStyleSheet(
            """
            QLabel#videoViewport {
                background: #070b12;
                color: #cbd2dc;
                border: 1px solid #2f3642;
                border-radius: 4px;
            }
            """
        )

        layout = QVBoxLayout(self)
        layout.setContentsMargins(14, 14, 14, 14)
        layout.addWidget(
            self.label,
            0,
            Qt.AlignmentFlag.AlignHCenter | Qt.AlignmentFlag.AlignTop,
        )

    def set_frame(self, image: QImage) -> None:
        self._image = image
        self._update_viewport_size()
        self._render()

    def clear(self) -> None:
        self._image = None
        self.label.setText("等待视频流")
        self.label.setPixmap(QPixmap())

    def hasHeightForWidth(self) -> bool:  # noqa: N802
        return True

    def heightForWidth(self, width: int) -> int:  # noqa: N802
        margins = self.layout().contentsMargins()
        content_width = max(1, width - margins.left() - margins.right())
        return int(content_width / self._aspect_ratio) + margins.top() + margins.bottom()

    def sizeHint(self) -> QSize:  # noqa: N802
        return QSize(960, self.heightForWidth(960))

    def resizeEvent(self, event) -> None:  # noqa: N802
        super().resizeEvent(event)
        self._update_viewport_size()
        self._render()

    def _update_viewport_size(self) -> None:
        margins = self.layout().contentsMargins()
        available_width = max(1, self.width() - margins.left() - margins.right())
        available_height = max(1, self.height() - margins.top() - margins.bottom())

        target_width = available_width
        target_height = int(target_width / self._aspect_ratio)
        if target_height > available_height:
            target_height = available_height
            target_width = int(target_height * self._aspect_ratio)

        self.label.setFixedSize(max(1, target_width), max(1, target_height))

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
