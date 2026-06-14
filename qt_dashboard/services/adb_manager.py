from __future__ import annotations

import os
import shlex
import subprocess
import time
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
        self.runtime_process: Optional[subprocess.Popen] = None

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
            return 127, "", f"未找到 adb：{self.adb_path}"
        except subprocess.TimeoutExpired:
            return 124, "", "adb 命令超时"

        return completed.returncode, completed.stdout.strip(), completed.stderr.strip()

    def devices(self) -> List[AdbDevice]:
        code, stdout, stderr = self.run(["devices"], timeout=6.0)
        if code != 0:
            raise RuntimeError(stderr or stdout or "adb devices 执行失败")

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
            raise RuntimeError(f"没有在线 ADB 设备（{states}）")
        raise RuntimeError("未检测到 ADB 设备")

    def forward(self, local_port: int, remote_port: int, serial: Optional[str] = None) -> None:
        args = []
        if serial:
            args.extend(["-s", serial])
        args.extend(["forward", f"tcp:{local_port}", f"tcp:{remote_port}"])
        code, stdout, stderr = self.run(args, timeout=6.0)
        if code != 0:
            raise RuntimeError(stderr or stdout or "adb forward 执行失败")

    def shell(self, command: str, serial: Optional[str] = None, timeout: float = 8.0) -> Tuple[int, str, str]:
        args = []
        if serial:
            args.extend(["-s", serial])
        args.extend(["shell", command])
        return self.run(args, timeout=timeout)

    def start_board_runtime(self, runtime_dir: str, serial: Optional[str] = None) -> None:
        if self.runtime_process is not None and self.runtime_process.poll() is None:
            return

        quoted_dir = shlex.quote(runtime_dir.rstrip("/") or "/userdata/aicam")
        command = (
            f"cd {quoted_dir} && "
            "mkdir -p logs && "
            "chmod +x camera camera_gateway start_aicam.sh && "
            "bash ./start_aicam.sh > logs/startup.out 2>&1"
        )
        args = []
        if serial:
            args.extend(["-s", serial])
        args.extend(["shell", command])

        try:
            self.runtime_process = subprocess.Popen(
                [self.adb_path, *args],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                text=True,
            )
        except FileNotFoundError as exc:
            raise RuntimeError(f"未找到 adb：{self.adb_path}") from exc

        time.sleep(0.5)
        if self.runtime_process.poll() is not None:
            self.runtime_process = None
            raise RuntimeError("板端运行进程立即退出")

    def stop_board_runtime(self, serial: Optional[str] = None) -> None:
        command = "killall camera camera_gateway 2>/dev/null || true"
        code, stdout, stderr = self.shell(command, serial=serial, timeout=6.0)
        if self.runtime_process is not None and self.runtime_process.poll() is None:
            self.runtime_process.terminate()
            try:
                self.runtime_process.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                self.runtime_process.kill()
        self.runtime_process = None
        if code != 0:
            raise RuntimeError(stderr or stdout or "停止板端运行进程失败")

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
