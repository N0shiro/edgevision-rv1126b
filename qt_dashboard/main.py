from __future__ import annotations

import sys
from collections import Counter
from pathlib import Path
from typing import Any, Callable, List, Optional

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from PySide6.QtCore import QEvent, QThread, QTimer, Qt, Signal
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
from qt_dashboard.services.config import DashboardDefaults, ensure_runtime_dirs
from qt_dashboard.services.csv_exporter import export_detection_events
from qt_dashboard.services.jsonl_reader import DetectionEvent
from qt_dashboard.services.log_watcher import LogWatcher
from qt_dashboard.services.video_worker import VideoWorker
from qt_dashboard.widgets.event_table import EventTable
from qt_dashboard.widgets.metrics_panel import MetricsPanel
from qt_dashboard.widgets.summary_panel import SummaryPanel
from qt_dashboard.widgets.video_panel import VideoPanel


class AdbTask(QThread):
    succeeded = Signal(object)
    failed = Signal(str)

    def __init__(self, task: Callable[[], Any], parent=None) -> None:
        super().__init__(parent)
        self.task = task

    def run(self) -> None:
        try:
            result = self.task()
        except RuntimeError as exc:
            self.failed.emit(str(exc))
        except Exception as exc:  # noqa: BLE001
            self.failed.emit(f"{type(exc).__name__}: {exc}")
        else:
            self.succeeded.emit(result)


class DashboardWindow(QMainWindow):
    MODE_ADB = "adb"
    MODE_LAN = "lan"
    MODE_OFFLINE = "offline"

    def __init__(self) -> None:
        super().__init__()
        ensure_runtime_dirs()

        self.defaults = DashboardDefaults()
        self.adb_manager = AdbManager()
        self.adb_serial: Optional[str] = None
        self.adb_task: Optional[AdbTask] = None
        self._adb_busy = False
        self.video_worker: Optional[VideoWorker] = None
        self.log_watcher: Optional[LogWatcher] = None
        self.events: List[DetectionEvent] = []
        self.counts: Counter[str] = Counter()

        self.setWindowTitle("AICAM 上位机")
        self.setMinimumSize(900, 560)
        self._build_ui()
        self._apply_style()
        self._fit_to_available_geometry()
        self._update_control_state()
        QTimer.singleShot(0, lambda: self.refresh_adb_devices(show_status=False))

    def closeEvent(self, event) -> None:  # noqa: N802
        if self.adb_task is not None and self.adb_task.isRunning():
            event.ignore()
            self._set_status("ADB 操作正在执行，请稍候再关闭")
            return
        self.stop_all()
        super().closeEvent(event)

    def changeEvent(self, event) -> None:  # noqa: N802
        super().changeEvent(event)
        if event.type() == QEvent.Type.WindowStateChange:
            QTimer.singleShot(0, self._refresh_splitter_balance)

    def _build_ui(self) -> None:
        self.mode_combo = QComboBox()
        self.mode_combo.addItem("ADB USB", self.MODE_ADB)
        self.mode_combo.addItem("局域网 URL", self.MODE_LAN)
        self.mode_combo.addItem("离线日志", self.MODE_OFFLINE)
        self.mode_combo.currentTextChanged.connect(self._on_mode_changed)

        self.adb_device_combo = QComboBox()
        self.adb_device_combo.setMinimumWidth(180)
        self.refresh_adb_button = QPushButton("刷新")
        self.refresh_adb_button.clicked.connect(self.refresh_adb_devices)

        self.url_edit = QLineEdit(self.defaults.adb_video_url)
        self.local_events_edit = QLineEdit(str(self.defaults.local_events_path))
        self.local_metrics_edit = QLineEdit(str(self.defaults.local_metrics_path))
        self.board_runtime_dir_edit = QLineEdit(self.defaults.board_runtime_dir)
        self.remote_log_dir_edit = QLineEdit(self.defaults.board_log_dir)
        for edit in (
            self.url_edit,
            self.local_events_edit,
            self.local_metrics_edit,
            self.board_runtime_dir_edit,
            self.remote_log_dir_edit,
        ):
            edit.setReadOnly(True)

        self.adb_button = QPushButton("连接 ADB")
        self.adb_button.clicked.connect(self.connect_adb)
        self.start_button = QPushButton("启动板端")
        self.start_button.clicked.connect(self.start_all)
        self.stop_button = QPushButton("停止板端")
        self.stop_button.clicked.connect(self.stop_all)
        self.pull_button = QPushButton("拉取日志")
        self.pull_button.clicked.connect(self.pull_logs_once)
        self.export_button = QPushButton("导出 CSV")
        self.export_button.clicked.connect(self.export_csv)
        self.clear_button = QPushButton("清空视图")
        self.clear_button.clicked.connect(self.clear_view)

        self.events_browse_button = QPushButton("选择")
        self.events_browse_button.clicked.connect(self.browse_events_path)
        self.metrics_browse_button = QPushButton("选择")
        self.metrics_browse_button.clicked.connect(self.browse_metrics_path)

        top_bar = self._build_top_bar()
        self.video_panel = VideoPanel()
        self.event_table = EventTable()
        self.summary_panel = SummaryPanel()
        self.metrics_panel = MetricsPanel()

        right_tabs = QTabWidget()
        right_tabs.addTab(self.event_table, "检测事件")
        right_tabs.addTab(self.summary_panel, "类别统计")
        right_tabs.addTab(self._build_task_panel(), "参数 / 日志")
        right_tabs.setMinimumWidth(320)

        self.top_splitter = QSplitter(Qt.Orientation.Horizontal)
        self.top_splitter.addWidget(self.video_panel)
        self.top_splitter.addWidget(right_tabs)
        self.top_splitter.setChildrenCollapsible(False)
        self.top_splitter.setSizes([1080, 360])
        self.top_splitter.setStretchFactor(0, 1)

        self.main_splitter = QSplitter(Qt.Orientation.Vertical)
        self.main_splitter.addWidget(self.top_splitter)
        self.main_splitter.addWidget(self.metrics_panel)
        self.main_splitter.setChildrenCollapsible(False)
        self.main_splitter.setSizes([620, 190])
        self.main_splitter.setStretchFactor(0, 3)
        self.main_splitter.setStretchFactor(1, 1)

        root = QWidget()
        root.setObjectName("rootPanel")
        root_layout = QVBoxLayout(root)
        root_layout.setContentsMargins(14, 12, 14, 8)
        root_layout.setSpacing(10)
        root_layout.addWidget(top_bar)
        root_layout.addWidget(self.main_splitter, 1)

        self.setCentralWidget(root)
        self.setStatusBar(QStatusBar())
        self.statusBar().showMessage("就绪")

    def _fit_to_available_geometry(self) -> None:
        screen = self.screen() or QApplication.primaryScreen()
        if screen is None:
            self.resize(1280, 760)
            return

        available = screen.availableGeometry()
        margin = 8
        max_width = max(640, available.width() - margin * 2)
        max_height = max(480, available.height() - margin * 2)
        width = min(1440, max_width, max(900, int(available.width() * 0.92)))
        height = min(860, max_height, max(560, int(available.height() * 0.90)))
        x = available.x() + max(0, (available.width() - width) // 2)
        y = available.y() + max(0, (available.height() - height) // 2)
        self.setGeometry(x, y, width, height)
        self._refresh_splitter_balance()

    def _refresh_splitter_balance(self) -> None:
        if not hasattr(self, "main_splitter"):
            return
        available_height = max(1, self.centralWidget().height())
        metrics_height = min(230, max(155, int(available_height * 0.24)))
        self.main_splitter.setSizes([max(1, available_height - metrics_height), metrics_height])

    def _build_top_bar(self) -> QWidget:
        panel = QWidget()
        panel.setObjectName("topBar")
        layout = QHBoxLayout(panel)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(10)

        adb_device_row = QWidget()
        adb_device_layout = QHBoxLayout(adb_device_row)
        adb_device_layout.setContentsMargins(0, 0, 0, 0)
        adb_device_layout.setSpacing(6)
        adb_device_layout.addWidget(self.adb_device_combo, 1)
        adb_device_layout.addWidget(self.refresh_adb_button)

        title_box = QWidget()
        title_layout = QVBoxLayout(title_box)
        title_layout.setContentsMargins(0, 0, 12, 0)
        title_layout.setSpacing(2)
        title = QLabel("AICAM 实时预览")
        title.setObjectName("pageTitle")
        subtitle = QLabel("ADB 一键启动板端运行包，自动端口转发并读取视频流与 JSONL 日志")
        subtitle.setObjectName("pageSubtitle")
        title_layout.addWidget(title)
        title_layout.addWidget(subtitle)

        url_label = QLabel("链接")
        mode_label = QLabel("模式")
        device_label = QLabel("设备")
        for label in (url_label, mode_label, device_label):
            label.setObjectName("topLabel")

        self.url_edit.setMinimumWidth(230)
        self.url_edit.setMaximumWidth(360)
        self.mode_combo.setMaximumWidth(140)
        adb_device_row.setMinimumWidth(260)

        layout.addWidget(title_box, 1)
        layout.addWidget(url_label)
        layout.addWidget(self.url_edit)
        layout.addWidget(mode_label)
        layout.addWidget(self.mode_combo)
        layout.addWidget(device_label)
        layout.addWidget(adb_device_row)
        layout.addWidget(self.adb_button)
        layout.addWidget(self.start_button)
        layout.addWidget(self.stop_button)
        return panel

    def _build_task_panel(self) -> QWidget:
        panel = QWidget()
        panel.setObjectName("taskPanel")
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(10, 10, 10, 10)
        layout.setSpacing(12)

        params_box = QGroupBox("参数设置")
        params_form = QFormLayout(params_box)
        params_form.setLabelAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        params_form.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.ExpandingFieldsGrow)
        params_form.setHorizontalSpacing(10)
        params_form.setVerticalSpacing(10)
        params_form.addRow("板端目录", self.board_runtime_dir_edit)
        params_form.addRow("板端日志", self.remote_log_dir_edit)

        events_row = QWidget()
        events_layout = QHBoxLayout(events_row)
        events_layout.setContentsMargins(0, 0, 0, 0)
        events_layout.setSpacing(8)
        events_layout.addWidget(self.local_events_edit)
        events_layout.addWidget(self.events_browse_button)

        metrics_row = QWidget()
        metrics_layout = QHBoxLayout(metrics_row)
        metrics_layout.setContentsMargins(0, 0, 0, 0)
        metrics_layout.setSpacing(8)
        metrics_layout.addWidget(self.local_metrics_edit)
        metrics_layout.addWidget(self.metrics_browse_button)

        logs_box = QGroupBox("日志与导出")
        logs_form = QFormLayout(logs_box)
        logs_form.setLabelAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        logs_form.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.ExpandingFieldsGrow)
        logs_form.setHorizontalSpacing(10)
        logs_form.setVerticalSpacing(10)

        log_action_row = QWidget()
        log_action_layout = QHBoxLayout(log_action_row)
        log_action_layout.setContentsMargins(0, 0, 0, 0)
        log_action_layout.setSpacing(8)
        log_action_layout.addWidget(self.pull_button)
        log_action_layout.addWidget(self.export_button)
        log_action_layout.addWidget(self.clear_button)

        logs_form.addRow("事件", events_row)
        logs_form.addRow("性能", metrics_row)
        logs_form.addRow("操作", log_action_row)

        layout.addWidget(params_box)
        layout.addWidget(logs_box)
        layout.addStretch(1)
        return panel

    def _apply_style(self) -> None:
        self.setStyleSheet(
            """
            QMainWindow { background: #0f1115; }
            QWidget { font-size: 13px; color: #e6e8ec; }
            QWidget#rootPanel { background: #0f1115; }
            QWidget#topBar {
                background: #121820;
                border: 1px solid #303744;
                border-radius: 6px;
                padding: 10px;
            }
            QWidget#taskPanel { background: #151922; }
            QLabel#pageTitle {
                color: #f8fafc;
                font-size: 20px;
                font-weight: 700;
            }
            QLabel#pageSubtitle {
                color: #9aa4b2;
                font-size: 12px;
            }
            QLabel#topLabel {
                color: #cbd2dc;
                font-weight: 600;
            }
            QGroupBox {
                border: 1px solid #334050;
                border-radius: 6px;
                margin-top: 10px;
                padding: 10px;
                color: #eef2f7;
                font-weight: 600;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 10px;
                padding: 0 5px;
            }
            QLineEdit, QComboBox {
                background: #191d25;
                color: #f3f4f6;
                border: 1px solid #343b49;
                border-radius: 4px;
                padding: 6px 8px;
                min-height: 18px;
            }
            QLineEdit:read-only {
                background: #151922;
                color: #aab2bf;
            }
            QPushButton {
                background: #0f766e;
                color: white;
                border: 0;
                border-radius: 4px;
                padding: 7px 10px;
                min-height: 20px;
            }
            QPushButton:hover { background: #0d9488; }
            QPushButton:pressed { background: #115e59; }
            QPushButton:disabled { background: #313846; color: #858c98; }
            QSplitter::handle { background: #1a1f29; }
            QTableWidget {
                background: #151922;
                alternate-background-color: #1a202b;
                color: #e6e8ec;
                gridline-color: #2f3642;
                selection-background-color: #0f766e;
                border: 1px solid #303744;
            }
            QHeaderView::section {
                background: #202632;
                color: #eef2f7;
                padding: 5px;
                border: 0;
                border-right: 1px solid #303744;
            }
            QTabWidget::pane { border: 1px solid #303744; }
            QTabBar::tab {
                background: #202632;
                color: #cbd2dc;
                padding: 8px 14px;
                border-top-left-radius: 4px;
                border-top-right-radius: 4px;
            }
            QTabBar::tab:selected { background: #0f766e; color: white; }
            QStatusBar { background: #151922; color: #cbd2dc; }
            """
        )

    def _on_mode_changed(self, mode: str) -> None:
        mode_key = self._current_mode()
        if mode_key == self.MODE_ADB:
            self.url_edit.setText(self.defaults.adb_video_url)
        elif mode_key == self.MODE_LAN:
            self.url_edit.setText(self.defaults.lan_video_url)
        self._update_control_state()

    def _update_control_state(self) -> None:
        mode_key = self._current_mode()
        is_adb = mode_key == self.MODE_ADB
        is_offline = mode_key == self.MODE_OFFLINE
        can_run_adb = is_adb and not self._adb_busy
        can_run = not self._adb_busy
        self.adb_button.setEnabled(can_run_adb)
        self.pull_button.setEnabled(can_run_adb)
        self.adb_device_combo.setEnabled(can_run_adb)
        self.refresh_adb_button.setEnabled(can_run_adb)
        self.board_runtime_dir_edit.setEnabled(can_run_adb)
        self.remote_log_dir_edit.setEnabled(can_run_adb)
        self.start_button.setEnabled(can_run)
        self.stop_button.setEnabled(can_run)
        self.url_edit.setEnabled(not is_offline and can_run)
        self.url_edit.setReadOnly(mode_key != self.MODE_LAN)

    def _set_adb_busy(self, busy: bool) -> None:
        self._adb_busy = busy
        self._update_control_state()

    def _current_mode(self) -> str:
        return str(self.mode_combo.currentData() or self.MODE_ADB)

    def _selected_adb_serial(self) -> Optional[str]:
        serial = self.adb_device_combo.currentData()
        if serial:
            return str(serial)
        return None

    def _run_adb_task(
        self,
        status_text: str,
        task: Callable[[], Any],
        on_success: Callable[[Any], None],
        error_title: str,
        show_error: bool = True,
    ) -> None:
        if self.adb_task is not None and self.adb_task.isRunning():
            self._set_status("ADB 操作正在执行，请稍候")
            return

        self._set_status(status_text)
        self._set_adb_busy(True)
        worker = AdbTask(task, self)
        self.adb_task = worker
        worker.succeeded.connect(lambda result: self._finish_adb_task(result, on_success))
        worker.failed.connect(lambda message: self._fail_adb_task(error_title, message, show_error))
        worker.finished.connect(worker.deleteLater)
        worker.start()

    def _finish_adb_task(self, result: Any, on_success: Callable[[Any], None]) -> None:
        self.adb_task = None
        self._set_adb_busy(False)
        on_success(result)

    def _fail_adb_task(self, title: str, message: str, show_error: bool) -> None:
        self.adb_task = None
        self._set_adb_busy(False)
        if title == "ADB 设备刷新失败":
            self.adb_device_combo.clear()
            self.adb_device_combo.addItem("未检测到设备", "")
        if show_error:
            self._show_error(title, message)
        else:
            self._set_status(message)

    def _connect_adb_blocking(self, selected_serial: Optional[str]) -> str:
        if selected_serial is None:
            device = self.adb_manager.first_online_device()
            selected_serial = device.serial
        else:
            states = {device.serial: device.state for device in self.adb_manager.devices()}
            state = states.get(selected_serial)
            if state != "device":
                raise RuntimeError(f"ADB 设备未在线：{selected_serial}（{state or '未知'}）")

        self.adb_manager.forward(
            self.defaults.adb_local_port,
            self.defaults.gateway_port,
            serial=selected_serial,
        )
        return selected_serial

    def refresh_adb_devices(self, show_status: bool = True) -> None:
        current_serial = self.adb_serial or self._selected_adb_serial()
        self._run_adb_task(
            "正在刷新 ADB 设备",
            self.adb_manager.devices,
            lambda devices: self._populate_adb_devices(devices, current_serial, show_status),
            "ADB 设备刷新失败",
            show_error=show_status,
        )

    def _populate_adb_devices(
        self,
        devices: List,
        current_serial: Optional[str],
        show_status: bool = True,
    ) -> None:
        self.adb_device_combo.clear()
        if not devices:
            self.adb_device_combo.addItem("未检测到设备", "")
            if show_status:
                self._set_status("未检测到 ADB 设备")
            return

        preferred_index = 0
        first_online_index: Optional[int] = None
        first_usb_index: Optional[int] = None
        for index, device in enumerate(devices):
            state_text = "在线" if device.state == "device" else device.state
            self.adb_device_combo.addItem(f"{device.serial}（{state_text}）", device.serial)
            if device.serial == current_serial:
                preferred_index = index
            if device.state == "device" and first_online_index is None:
                first_online_index = index
            if (
                device.state == "device"
                and first_usb_index is None
                and not device.serial.startswith("127.")
                and not device.serial.startswith("emulator-")
            ):
                first_usb_index = index

        if current_serial is None:
            preferred_index = (
                first_usb_index
                if first_usb_index is not None
                else first_online_index
                if first_online_index is not None
                else 0
            )
        self.adb_device_combo.setCurrentIndex(preferred_index)
        if show_status:
            self._set_status(f"已刷新 ADB 设备：{len(devices)} 个")

    def connect_adb(self) -> None:
        selected_serial = self._selected_adb_serial()
        self._run_adb_task(
            "正在连接 ADB 并建立端口转发",
            lambda: self._connect_adb_blocking(selected_serial),
            self._on_adb_connected,
            "ADB 连接失败",
        )

    def _on_adb_connected(self, selected_serial: str) -> None:
        self.adb_serial = selected_serial
        self.url_edit.setText(self.defaults.adb_video_url)
        self._set_status(f"ADB 已连接：{selected_serial}，端口转发到 {self.defaults.adb_video_url}")

    def pull_logs_once(self) -> None:
        if self._current_mode() != self.MODE_ADB:
            return

        selected_serial = self.adb_serial or self._selected_adb_serial()
        remote_log_dir = self.remote_log_dir_edit.text().strip() or self.defaults.board_log_dir
        local_log_dir = Path(self.local_events_edit.text()).parent

        def task() -> tuple[str, bool, bool]:
            serial = self._connect_adb_blocking(selected_serial)
            events_ok, metrics_ok = self.adb_manager.pull_logs(
                remote_log_dir,
                local_log_dir,
                serial=serial,
            )
            return serial, events_ok, metrics_ok

        self._run_adb_task(
            "正在拉取板端日志",
            task,
            self._on_logs_pulled,
            "日志拉取失败",
        )

    def _on_logs_pulled(self, result: tuple[str, bool, bool]) -> None:
        serial, events_ok, metrics_ok = result
        self.adb_serial = serial
        events_text = "成功" if events_ok else "失败"
        metrics_text = "成功" if metrics_ok else "失败"
        self._set_status(f"日志已拉取：事件={events_text} 性能={metrics_text}")

    def start_all(self) -> None:
        self.stop_local_workers()
        mode = self._current_mode()
        if mode == self.MODE_ADB:
            selected_serial = self.adb_serial or self._selected_adb_serial()
            runtime_dir = self.board_runtime_dir_edit.text().strip() or self.defaults.board_runtime_dir

            def task() -> str:
                serial = self._connect_adb_blocking(selected_serial)
                self.adb_manager.start_board_runtime(
                    runtime_dir,
                    serial=serial,
                )
                return serial

            self._run_adb_task(
                "正在连接 ADB、同步板端时间并启动运行包",
                task,
                self._on_board_started,
                "板端启动失败",
            )
            return

        self._start_local_workers(mode)

    def _on_board_started(self, serial: str) -> None:
        self.adb_serial = serial
        self.url_edit.setText(self.defaults.adb_video_url)
        self._set_status("板端运行包启动中")
        self._start_local_workers(self.MODE_ADB)

    def _start_local_workers(self, mode: str) -> None:
        if mode != self.MODE_OFFLINE:
            self.video_worker = VideoWorker(self.url_edit.text().strip())
            self.video_worker.frame_ready.connect(self.video_panel.set_frame)
            self.video_worker.status_changed.connect(self._set_status)
            self.video_worker.start()

        self.log_watcher = LogWatcher(
            events_path=Path(self.local_events_edit.text()),
            metrics_path=Path(self.local_metrics_edit.text()),
            use_adb_pull=(mode == self.MODE_ADB),
            adb_manager=self.adb_manager,
            adb_serial=self.adb_serial,
            board_log_dir=self.remote_log_dir_edit.text().strip() or self.defaults.board_log_dir,
        )
        self.log_watcher.events_ready.connect(self.on_events)
        self.log_watcher.metrics_ready.connect(self.metrics_panel.append_samples)
        self.log_watcher.status_changed.connect(self._set_status)
        self.log_watcher.start()
        self._set_status(f"已启动：{self.mode_combo.currentText()}")

    def stop_local_workers(self) -> None:
        if self.video_worker is not None:
            self.video_worker.stop()
            self.video_worker = None
        if self.log_watcher is not None:
            self.log_watcher.stop()
            self.log_watcher = None

    def stop_all(self) -> None:
        self.stop_local_workers()
        if self._current_mode() == self.MODE_ADB and self.adb_serial is not None:
            try:
                self.adb_manager.stop_board_runtime(serial=self.adb_serial)
            except RuntimeError as exc:
                self._set_status(str(exc))
                return
        self._set_status("已停止")

    def clear_view(self) -> None:
        self.events.clear()
        self.counts.clear()
        self.event_table.clear()
        self.summary_panel.clear()
        self.metrics_panel.clear()
        self.video_panel.clear()
        self._set_status("视图已清空")

    def browse_events_path(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self,
            "选择 events.jsonl",
            str(Path(self.local_events_edit.text()).parent),
            "JSONL 文件 (*.jsonl);;所有文件 (*.*)",
        )
        if path:
            self.local_events_edit.setText(path)

    def browse_metrics_path(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self,
            "选择 metrics.jsonl",
            str(Path(self.local_metrics_edit.text()).parent),
            "JSONL 文件 (*.jsonl);;所有文件 (*.*)",
        )
        if path:
            self.local_metrics_edit.setText(path)

    def export_csv(self) -> None:
        if not self.events:
            self._set_status("没有可导出的检测事件")
            return
        path, _ = QFileDialog.getSaveFileName(
            self,
            "导出检测事件 CSV",
            str(Path("aicam_events.csv").resolve()),
            "CSV 文件 (*.csv);;所有文件 (*.*)",
        )
        if not path:
            return
        export_detection_events(Path(path), self.events)
        self._set_status(f"已导出：{path}")

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
