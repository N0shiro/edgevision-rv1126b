from __future__ import annotations

from collections import deque
from typing import Deque, Iterable

from PySide6.QtWidgets import QGridLayout, QLabel, QVBoxLayout, QWidget

from qt_dashboard.services.jsonl_reader import MetricsSample


class MetricsPanel(QWidget):
    def __init__(self, max_points: int = 120, parent=None) -> None:
        super().__init__(parent)
        import pyqtgraph as pg

        self.max_points = max_points
        self.index = 0
        self.x_values: Deque[int] = deque(maxlen=max_points)
        self.capture_values: Deque[float] = deque(maxlen=max_points)
        self.encode_values: Deque[float] = deque(maxlen=max_points)
        self.ai_values: Deque[float] = deque(maxlen=max_points)
        self.cpu_values: Deque[float] = deque(maxlen=max_points)
        self.infer_values: Deque[float] = deque(maxlen=max_points)

        self.capture_label = QLabel("Capture FPS: 0.00")
        self.encode_label = QLabel("Encode FPS: 0.00")
        self.ai_label = QLabel("AI FPS: 0.00")
        self.cpu_label = QLabel("CPU: 0.00%")
        self.rss_label = QLabel("RSS: 0.00 MB")
        self.infer_label = QLabel("Infer: 0.00 ms")
        self.drop_label = QLabel("Drops: enc 0 / ai 0")

        grid = QGridLayout()
        labels = [
            self.capture_label,
            self.encode_label,
            self.ai_label,
            self.cpu_label,
            self.rss_label,
            self.infer_label,
            self.drop_label,
        ]
        for idx, label in enumerate(labels):
            label.setMinimumWidth(150)
            grid.addWidget(label, idx // 4, idx % 4)

        self.plot = pg.PlotWidget()
        self.plot.setBackground("#0f172a")
        self.plot.showGrid(x=True, y=True, alpha=0.25)
        self.plot.addLegend(offset=(10, 10))
        self.plot.setLabel("left", "Value")
        self.plot.setLabel("bottom", "Sample")
        self.capture_curve = self.plot.plot(pen=pg.mkPen("#22c55e", width=2), name="capture fps")
        self.encode_curve = self.plot.plot(pen=pg.mkPen("#38bdf8", width=2), name="encode fps")
        self.ai_curve = self.plot.plot(pen=pg.mkPen("#f59e0b", width=2), name="ai fps")
        self.cpu_curve = self.plot.plot(pen=pg.mkPen("#ef4444", width=2), name="cpu %")
        self.infer_curve = self.plot.plot(pen=pg.mkPen("#a78bfa", width=2), name="infer ms")

        layout = QVBoxLayout(self)
        layout.addLayout(grid)
        layout.addWidget(self.plot)

    def append_samples(self, samples: Iterable[MetricsSample]) -> None:
        last_sample = None
        for sample in samples:
            last_sample = sample
            self.index += 1
            self.x_values.append(self.index)
            self.capture_values.append(sample.capture_fps)
            self.encode_values.append(sample.encode_fps)
            self.ai_values.append(sample.ai_fps)
            self.cpu_values.append(sample.cpu_percent)
            self.infer_values.append(sample.average_inference_ms)

        if last_sample is None:
            return

        self.capture_label.setText(f"Capture FPS: {last_sample.capture_fps:.2f}")
        self.encode_label.setText(f"Encode FPS: {last_sample.encode_fps:.2f}")
        self.ai_label.setText(f"AI FPS: {last_sample.ai_fps:.2f}")
        self.cpu_label.setText(f"CPU: {last_sample.cpu_percent:.2f}%")
        self.rss_label.setText(f"RSS: {last_sample.rss_mb:.2f} MB")
        self.infer_label.setText(f"Infer: {last_sample.average_inference_ms:.2f} ms")
        self.drop_label.setText(
            f"Drops: enc {last_sample.encode_queue_drops} / ai {last_sample.ai_queue_drops}"
        )
        self._refresh_plot()

    def clear(self) -> None:
        self.index = 0
        self.x_values.clear()
        self.capture_values.clear()
        self.encode_values.clear()
        self.ai_values.clear()
        self.cpu_values.clear()
        self.infer_values.clear()
        self._refresh_plot()

    def _refresh_plot(self) -> None:
        x = list(self.x_values)
        self.capture_curve.setData(x, list(self.capture_values))
        self.encode_curve.setData(x, list(self.encode_values))
        self.ai_curve.setData(x, list(self.ai_values))
        self.cpu_curve.setData(x, list(self.cpu_values))
        self.infer_curve.setData(x, list(self.infer_values))
