from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
DASHBOARD_ROOT = Path(__file__).resolve().parents[1]
RUNTIME_DIR = DASHBOARD_ROOT / "runtime"
RUNTIME_LOG_DIR = RUNTIME_DIR / "logs"
SAMPLE_DATA_DIR = DASHBOARD_ROOT / "sample_data"


@dataclass(frozen=True)
class DashboardDefaults:
    adb_local_port: int = 18080
    gateway_port: int = 8080
    board_runtime_dir: str = "/userdata/aicam"
    board_log_dir: str = "/userdata/aicam/logs"
    adb_video_url: str = "http://127.0.0.1:18080/"
    lan_video_url: str = "http://192.168.1.88:8080/"
    local_events_path: Path = RUNTIME_LOG_DIR / "events.jsonl"
    local_metrics_path: Path = RUNTIME_LOG_DIR / "metrics.jsonl"


def ensure_runtime_dirs() -> None:
    RUNTIME_LOG_DIR.mkdir(parents=True, exist_ok=True)
