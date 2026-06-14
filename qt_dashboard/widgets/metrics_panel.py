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

        self.capture_label = QLabel("采集 FPS：0.00")
        self.encode_label = QLabel("编码 FPS：0.00")
        self.ai_label = QLabel("AI FPS：0.00")
        self.cpu_label = QLabel("CPU: 0.00%")
        self.rss_label = QLabel("内存：0.00 MB")
        self.infer_label = QLabel("推理：0.00 ms")
        self.drop_label = QLabel("丢帧：编码 0 / AI 0")

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
        self.plot.setBackground("#11151c")
        self.plot.showGrid(x=True, y=True, alpha=0.25)
        self.plot.addLegend(offset=(10, 10))
        self.plot.setLabel("left", "数值")
        self.plot.setLabel("bottom", "采样")
        self.capture_curve = self.plot.plot(pen=pg.mkPen("#22c55e", width=2), name="采集 FPS")
        self.encode_curve = self.plot.plot(pen=pg.mkPen("#38bdf8", width=2), name="编码 FPS")
        self.ai_curve = self.plot.plot(pen=pg.mkPen("#f59e0b", width=2), name="AI FPS")
        self.cpu_curve = self.plot.plot(pen=pg.mkPen("#ef4444", width=2), name="CPU %")
        self.infer_curve = self.plot.plot(pen=pg.mkPen("#a78bfa", width=2), name="推理 ms")

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

        self.capture_label.setText(f"采集 FPS：{last_sample.capture_fps:.2f}")
        self.encode_label.setText(f"编码 FPS：{last_sample.encode_fps:.2f}")
        self.ai_label.setText(f"AI FPS：{last_sample.ai_fps:.2f}")
        self.cpu_label.setText(f"CPU: {last_sample.cpu_percent:.2f}%")
        self.rss_label.setText(f"内存：{last_sample.rss_mb:.2f} MB")
        self.infer_label.setText(f"推理：{last_sample.average_inference_ms:.2f} ms")
        self.drop_label.setText(
            f"丢帧：编码 {last_sample.encode_queue_drops} / AI {last_sample.ai_queue_drops}"
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
        self.capture_label.setText("采集 FPS：0.00")
        self.encode_label.setText("编码 FPS：0.00")
        self.ai_label.setText("AI FPS：0.00")
        self.cpu_label.setText("CPU: 0.00%")
        self.rss_label.setText("内存：0.00 MB")
        self.infer_label.setText("推理：0.00 ms")
        self.drop_label.setText("丢帧：编码 0 / AI 0")
        self._refresh_plot()

    def _refresh_plot(self) -> None:
        x = list(self.x_values)
        self.capture_curve.setData(x, list(self.capture_values))
        self.encode_curve.setData(x, list(self.encode_values))
        self.ai_curve.setData(x, list(self.ai_values))
        self.cpu_curve.setData(x, list(self.cpu_values))
        self.infer_curve.setData(x, list(self.infer_values))
