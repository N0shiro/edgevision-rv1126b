from __future__ import annotations

from collections import Counter
from typing import Mapping

from PySide6.QtWidgets import QLabel, QTableWidget, QTableWidgetItem, QVBoxLayout, QWidget


class SummaryPanel(QWidget):
    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.total_label = QLabel("检测事件：0")
        self.counts_table = QTableWidget(0, 2)
        self.counts_table.setHorizontalHeaderLabels(["类别", "数量"])
        self.counts_table.verticalHeader().setVisible(False)
        self.counts_table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.counts_table.horizontalHeader().setStretchLastSection(True)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self.total_label)
        layout.addWidget(self.counts_table)

    def update_counts(self, counts: Mapping[str, int] | Counter, total: int) -> None:
        self.total_label.setText(f"检测事件：{total}")
        ordered = sorted(counts.items(), key=lambda item: (-item[1], item[0]))
        self.counts_table.setRowCount(len(ordered))
        for row, (label, count) in enumerate(ordered):
            self.counts_table.setItem(row, 0, QTableWidgetItem(label))
            self.counts_table.setItem(row, 1, QTableWidgetItem(str(count)))

    def clear(self) -> None:
        self.total_label.setText("检测事件：0")
        self.counts_table.setRowCount(0)
