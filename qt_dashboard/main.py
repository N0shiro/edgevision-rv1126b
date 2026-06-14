from __future__ import annotations

import sys
from collections import Counter
from pathlib import Path
from typing import List, Optional

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QApplication,
    QComboBox,
    QFileDialog,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QSplitter,
    QStatusBar,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)

from qt_dashboard.services.adb_manager import AdbManager
from qt_dashboard.services.config import DashboardDefaults, RUNTIME_LOG_DIR, ensure_runtime_dirs
from qt_dashboard.services.csv_exporter import export_detection_events
from qt_dashboard.services.jsonl_reader import DetectionEvent
from qt_dashboard.services.log_watcher import LogWatcher
from qt_dashboard.services.video_worker import VideoWorker
from qt_dashboard.widgets.event_table import EventTable
from qt_dashboard.widgets.metrics_panel import MetricsPanel
from qt_dashboard.widgets.summary_panel import SummaryPanel
from qt_dashboard.widgets.video_panel import VideoPanel


class DashboardWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        ensure_runtime_dirs()

        self.defaults = DashboardDefaults()
        self.adb_manager = AdbManager()
        self.adb_serial: Optional[str] = None
        self.video_worker: Optional[VideoWorker] = None
        self.log_watcher: Optional[LogWatcher] = None
        self.events: List[DetectionEvent] = []
        self.counts: Counter[str] = Counter()

        self.setWindowTitle("AICAM Dashboard")
        self.resize(1440, 900)
        self._build_ui()
        self._apply_style()

    def closeEvent(self, event) -> None:  # noqa: N802
        self.stop_all()
        super().closeEvent(event)

    def _build_ui(self) -> None:
        self.mode_combo = QComboBox()
        self.mode_combo.addItems(["ADB USB", "LAN URL", "Offline Logs"])
        self.mode_combo.currentTextChanged.connect(self._on_mode_changed)

        self.url_edit = QLineEdit(self.defaults.adb_video_url)
        self.local_events_edit = QLineEdit(str(self.defaults.local_events_path))
        self.local_metrics_edit = QLineEdit(str(self.defaults.local_metrics_path))
        self.board_runtime_dir_edit = QLineEdit(self.defaults.board_runtime_dir)
        self.remote_log_dir_edit = QLineEdit(self.defaults.board_log_dir)

        self.adb_button = QPushButton("ADB Connect")
        self.adb_button.clicked.connect(self.connect_adb)
        self.start_button = QPushButton("Start Runtime")
        self.start_button.clicked.connect(self.start_all)
        self.stop_button = QPushButton("Stop Runtime")
        self.stop_button.clicked.connect(self.stop_all)
        self.pull_button = QPushButton("Pull Logs")
        self.pull_button.clicked.connect(self.pull_logs_once)
        self.export_button = QPushButton("Export CSV")
        self.export_button.clicked.connect(self.export_csv)
        self.clear_button = QPushButton("Clear View")
        self.clear_button.clicked.connect(self.clear_view)

        self.events_browse_button = QPushButton("Browse")
        self.events_browse_button.clicked.connect(self.browse_events_path)
        self.metrics_browse_button = QPushButton("Browse")
        self.metrics_browse_button.clicked.connect(self.browse_metrics_path)

        controls = self._build_controls()
        self.video_panel = VideoPanel()
        self.event_table = EventTable()
        self.summary_panel = SummaryPanel()
        self.metrics_panel = MetricsPanel()

        right_tabs = QTabWidget()
        right_tabs.addTab(self.event_table, "Events")
        right_tabs.addTab(self.summary_panel, "Counts")

        top_splitter = QSplitter(Qt.Orientation.Horizontal)
        top_splitter.addWidget(controls)
        top_splitter.addWidget(self.video_panel)
        top_splitter.addWidget(right_tabs)
        top_splitter.setSizes([340, 760, 360])
        top_splitter.setStretchFactor(1, 1)

        main_splitter = QSplitter(Qt.Orientation.Vertical)
        main_splitter.addWidget(top_splitter)
        main_splitter.addWidget(self.metrics_panel)
        main_splitter.setSizes([610, 260])
        main_splitter.setStretchFactor(0, 2)

        self.setCentralWidget(main_splitter)
        self.setStatusBar(QStatusBar())
        self.statusBar().showMessage("Ready")

    def _build_controls(self) -> QWidget:
        panel = QWidget()
        layout = QVBoxLayout(panel)

        connection_box = QGroupBox("Connection")
        connection_form = QFormLayout(connection_box)
        connection_form.addRow("Mode", self.mode_combo)
        connection_form.addRow("Video URL", self.url_edit)
        connection_form.addRow("Board runtime", self.board_runtime_dir_edit)
        connection_form.addRow("Board logs", self.remote_log_dir_edit)
        connection_form.addRow(self.adb_button, self.pull_button)
        connection_form.addRow(self.start_button, self.stop_button)

        events_row = QWidget()
        events_layout = QHBoxLayout(events_row)
        events_layout.setContentsMargins(0, 0, 0, 0)
        events_layout.addWidget(self.local_events_edit)
        events_layout.addWidget(self.events_browse_button)

        metrics_row = QWidget()
        metrics_layout = QHBoxLayout(metrics_row)
        metrics_layout.setContentsMargins(0, 0, 0, 0)
        metrics_layout.addWidget(self.local_metrics_edit)
        metrics_layout.addWidget(self.metrics_browse_button)

        logs_box = QGroupBox("Local Logs")
        logs_form = QFormLayout(logs_box)
        logs_form.addRow("events.jsonl", events_row)
        logs_form.addRow("metrics.jsonl", metrics_row)
        logs_form.addRow(self.export_button, self.clear_button)

        hint = QLabel(
            "ADB mode starts the board runtime, maps board :8080 to local :18080, and pulls JSONL logs. "
            "LAN mode reads video directly. Offline mode only reads local logs."
        )
        hint.setWordWrap(True)
        hint.setObjectName("hintLabel")

        layout.addWidget(connection_box)
        layout.addWidget(logs_box)
        layout.addWidget(hint)
        layout.addStretch(1)
        return panel

    def _apply_style(self) -> None:
        self.setStyleSheet(
            """
            QMainWindow { background: #0b1120; }
            QWidget { font-size: 13px; }
            QGroupBox {
                border: 1px solid #334155;
                border-radius: 6px;
                margin-top: 10px;
                padding: 8px;
                color: #e5e7eb;
                font-weight: 600;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 8px;
                padding: 0 4px;
            }
            QLabel { color: #e5e7eb; }
            QLabel#hintLabel { color: #94a3b8; line-height: 140%; }
            QLineEdit, QComboBox {
                background: #111827;
                color: #e5e7eb;
                border: 1px solid #334155;
                border-radius: 4px;
                padding: 6px;
            }
            QPushButton {
                background: #2563eb;
                color: white;
                border: 0;
                border-radius: 4px;
                padding: 7px 10px;
            }
            QPushButton:hover { background: #1d4ed8; }
            QPushButton:pressed { background: #1e40af; }
            QTableWidget {
                background: #111827;
                alternate-background-color: #162033;
                color: #e5e7eb;
                gridline-color: #334155;
                selection-background-color: #1d4ed8;
                border: 1px solid #334155;
            }
            QHeaderView::section {
                background: #1e293b;
                color: #e5e7eb;
                padding: 5px;
                border: 0;
                border-right: 1px solid #334155;
            }
            QTabWidget::pane { border: 1px solid #334155; }
            QTabBar::tab {
                background: #1e293b;
                color: #cbd5e1;
                padding: 8px 14px;
                border-top-left-radius: 4px;
                border-top-right-radius: 4px;
            }
            QTabBar::tab:selected { background: #2563eb; color: white; }
            QStatusBar { background: #111827; color: #cbd5e1; }
            """
        )

    def _on_mode_changed(self, mode: str) -> None:
        if mode == "ADB USB":
            self.url_edit.setText(self.defaults.adb_video_url)
        elif mode == "LAN URL":
            self.url_edit.setText(self.defaults.lan_video_url)
        self.adb_button.setEnabled(mode == "ADB USB")
        self.pull_button.setEnabled(mode == "ADB USB")

    def connect_adb(self) -> None:
        try:
            device = self.adb_manager.first_online_device()
            self.adb_manager.forward(
                self.defaults.adb_local_port,
                self.defaults.gateway_port,
                serial=device.serial,
            )
        except RuntimeError as exc:
            self._show_error("ADB connection failed", str(exc))
            return

        self.adb_serial = device.serial
        self.url_edit.setText(self.defaults.adb_video_url)
        self._set_status(f"ADB connected: {device.serial}, forwarded to {self.defaults.adb_video_url}")

    def pull_logs_once(self) -> None:
        if self.mode_combo.currentText() != "ADB USB":
            return
        if self.adb_serial is None:
            self.connect_adb()
            if self.adb_serial is None:
                return

        events_ok, metrics_ok = self.adb_manager.pull_logs(
            self.remote_log_dir_edit.text().strip() or self.defaults.board_log_dir,
            Path(self.local_events_edit.text()).parent,
            serial=self.adb_serial,
        )
        self._set_status(f"pulled logs: events={events_ok} metrics={metrics_ok}")

    def start_all(self) -> None:
        self.stop_local_workers()
        mode = self.mode_combo.currentText()
        if mode == "ADB USB" and self.adb_serial is None:
            self.connect_adb()
            if self.adb_serial is None:
                return
        if mode == "ADB USB":
            try:
                self.adb_manager.start_board_runtime(
                    self.board_runtime_dir_edit.text().strip() or self.defaults.board_runtime_dir,
                    serial=self.adb_serial,
                )
            except RuntimeError as exc:
                self._show_error("Board runtime start failed", str(exc))
                return
            self._set_status("board runtime starting")

        if mode != "Offline Logs":
            self.video_worker = VideoWorker(self.url_edit.text().strip())
            self.video_worker.frame_ready.connect(self.video_panel.set_frame)
            self.video_worker.status_changed.connect(self._set_status)
            self.video_worker.start()

        self.log_watcher = LogWatcher(
            events_path=Path(self.local_events_edit.text()),
            metrics_path=Path(self.local_metrics_edit.text()),
            use_adb_pull=(mode == "ADB USB"),
            adb_manager=self.adb_manager,
            adb_serial=self.adb_serial,
            board_log_dir=self.remote_log_dir_edit.text().strip() or self.defaults.board_log_dir,
        )
        self.log_watcher.events_ready.connect(self.on_events)
        self.log_watcher.metrics_ready.connect(self.metrics_panel.append_samples)
        self.log_watcher.status_changed.connect(self._set_status)
        self.log_watcher.start()
        self._set_status(f"started in {mode} mode")

    def stop_local_workers(self) -> None:
        if self.video_worker is not None:
            self.video_worker.stop()
            self.video_worker = None
        if self.log_watcher is not None:
            self.log_watcher.stop()
            self.log_watcher = None

    def stop_all(self) -> None:
        self.stop_local_workers()
        if self.mode_combo.currentText() == "ADB USB" and self.adb_serial is not None:
            try:
                self.adb_manager.stop_board_runtime(serial=self.adb_serial)
            except RuntimeError as exc:
                self._set_status(str(exc))
                return
        self._set_status("stopped")

    def clear_view(self) -> None:
        self.events.clear()
        self.counts.clear()
        self.event_table.clear()
        self.summary_panel.clear()
        self.metrics_panel.clear()
        self.video_panel.clear()
        self._set_status("view cleared")

    def browse_events_path(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self,
            "Select events.jsonl",
            str(Path(self.local_events_edit.text()).parent),
            "JSONL files (*.jsonl);;All files (*.*)",
        )
        if path:
            self.local_events_edit.setText(path)

    def browse_metrics_path(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self,
            "Select metrics.jsonl",
            str(Path(self.local_metrics_edit.text()).parent),
            "JSONL files (*.jsonl);;All files (*.*)",
        )
        if path:
            self.local_metrics_edit.setText(path)

    def export_csv(self) -> None:
        if not self.events:
            self._set_status("no events to export")
            return
        path, _ = QFileDialog.getSaveFileName(
            self,
            "Export events CSV",
            str(Path("aicam_events.csv").resolve()),
            "CSV files (*.csv);;All files (*.*)",
        )
        if not path:
            return
        export_detection_events(Path(path), self.events)
        self._set_status(f"exported: {path}")

    def on_events(self, events: List[DetectionEvent]) -> None:
        self.events.extend(events)
        self.event_table.append_events(events)
        for event in events:
            self.counts[event.label] += 1
        self.summary_panel.update_counts(self.counts, len(self.events))

    def _set_status(self, text: str) -> None:
        self.statusBar().showMessage(text)

    def _show_error(self, title: str, message: str) -> None:
        self._set_status(message)
        QMessageBox.warning(self, title, message)


def main() -> int:
    app = QApplication(sys.argv)
    window = DashboardWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
