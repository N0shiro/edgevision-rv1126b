from __future__ import annotations

import os
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Optional, Tuple


@dataclass(frozen=True)
class AdbDevice:
    serial: str
    state: str


class AdbManager:
    def __init__(self, adb_path: Optional[str] = None) -> None:
        self.adb_path = adb_path or os.environ.get("AICAM_ADB", "adb")

    def run(self, args: Iterable[str], timeout: float = 8.0) -> Tuple[int, str, str]:
        command = [self.adb_path, *args]
        try:
            completed = subprocess.run(
                command,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=timeout,
                check=False,
            )
        except FileNotFoundError:
            return 127, "", f"adb not found: {self.adb_path}"
        except subprocess.TimeoutExpired:
            return 124, "", "adb command timed out"

        return completed.returncode, completed.stdout.strip(), completed.stderr.strip()

    def devices(self) -> List[AdbDevice]:
        code, stdout, stderr = self.run(["devices"], timeout=6.0)
        if code != 0:
            raise RuntimeError(stderr or stdout or "adb devices failed")

        devices: List[AdbDevice] = []
        for line in stdout.splitlines()[1:]:
            line = line.strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) >= 2:
                devices.append(AdbDevice(serial=parts[0], state=parts[1]))
        return devices

    def first_online_device(self) -> AdbDevice:
        devices = self.devices()
        for device in devices:
            if device.state == "device":
                return device
        if devices:
            states = ", ".join(f"{item.serial}:{item.state}" for item in devices)
            raise RuntimeError(f"no online adb device ({states})")
        raise RuntimeError("no adb device detected")

    def forward(self, local_port: int, remote_port: int, serial: Optional[str] = None) -> None:
        args = []
        if serial:
            args.extend(["-s", serial])
        args.extend(["forward", f"tcp:{local_port}", f"tcp:{remote_port}"])
        code, stdout, stderr = self.run(args, timeout=6.0)
        if code != 0:
            raise RuntimeError(stderr or stdout or "adb forward failed")

    def pull_file(self, remote_path: str, local_path: Path, serial: Optional[str] = None) -> bool:
        local_path.parent.mkdir(parents=True, exist_ok=True)
        args = []
        if serial:
            args.extend(["-s", serial])
        args.extend(["pull", remote_path, str(local_path)])
        code, _stdout, _stderr = self.run(args, timeout=12.0)
        return code == 0 and local_path.exists()

    def pull_logs(
        self,
        remote_log_dir: str,
        local_log_dir: Path,
        serial: Optional[str] = None,
    ) -> Tuple[bool, bool]:
        events_ok = self.pull_file(
            f"{remote_log_dir.rstrip('/')}/events.jsonl",
            local_log_dir / "events.jsonl",
            serial=serial,
        )
        metrics_ok = self.pull_file(
            f"{remote_log_dir.rstrip('/')}/metrics.jsonl",
            local_log_dir / "metrics.jsonl",
            serial=serial,
        )
        return events_ok, metrics_ok
