from __future__ import annotations

from typing import Iterable

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QHeaderView, QTableWidget, QTableWidgetItem, QWidget, QVBoxLayout

from qt_dashboard.services.jsonl_reader import DetectionEvent


class EventTable(QWidget):
    def __init__(self, max_rows: int = 500, parent=None) -> None:
        super().__init__(parent)
        self.max_rows = max_rows
        self.table = QTableWidget(0, 7)
        self.table.setHorizontalHeaderLabels(
            ["时间", "帧号", "类别", "置信度", "推理 ms", "检测框", "后端"]
        )
        self.table.verticalHeader().setVisible(False)
        self.table.setAlternatingRowColors(True)
        self.table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.table.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
        self.table.horizontalHeader().setStretchLastSection(True)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self.table)

    def append_events(self, events: Iterable[DetectionEvent]) -> None:
        for event in events:
            self._append_event(event)
        self.table.scrollToBottom()

    def clear(self) -> None:
        self.table.setRowCount(0)

    def _append_event(self, event: DetectionEvent) -> None:
        while self.table.rowCount() >= self.max_rows:
            self.table.removeRow(0)

        row = self.table.rowCount()
        self.table.insertRow(row)
        values = [
            event.timestamp,
            str(event.frame_sequence),
            event.label,
            f"{event.score:.3f}",
            f"{event.inference_ms:.2f}",
            f"({event.x1:.0f},{event.y1:.0f})-({event.x2:.0f},{event.y2:.0f})",
            event.backend,
        ]
        for column, value in enumerate(values):
            item = QTableWidgetItem(value)
            if column in (1, 3, 4):
                item.setTextAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
            self.table.setItem(row, column, item)
