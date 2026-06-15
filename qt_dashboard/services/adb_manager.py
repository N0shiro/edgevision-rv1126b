from __future__ import annotations

import os
import shlex
import shutil
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable, List, Optional, Tuple


@dataclass(frozen=True)
class AdbDevice:
    serial: str
    state: str


class AdbManager:
    def __init__(self, adb_path: Optional[str] = None) -> None:
        self.adb_path = self._resolve_adb_path(adb_path)
        self.runtime_process: Optional[subprocess.Popen] = None
        self._run_lock = threading.Lock()

    def _resolve_adb_path(self, adb_path: Optional[str]) -> str:
        configured_path = adb_path or os.environ.get("AICAM_ADB")
        if configured_path:
            return configured_path

        meipass = getattr(sys, "_MEIPASS", None)
        bundled_candidates = []
        if meipass:
            bundled_candidates.append(Path(meipass) / "adb.exe")
        bundled_candidates.append(Path(sys.executable).resolve().parent / "adb.exe")
        for candidate in bundled_candidates:
            if candidate.is_file():
                return str(candidate)

        path_adb = shutil.which("adb")
        if path_adb:
            return path_adb

        candidates = []
        for env_name in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
            value = os.environ.get(env_name)
            if value:
                candidates.append(Path(value) / "platform-tools" / "adb.exe")
        local_app_data = os.environ.get("LOCALAPPDATA")
        if local_app_data:
            candidates.append(Path(local_app_data) / "Android" / "Sdk" / "platform-tools" / "adb.exe")
        program_files = os.environ.get("ProgramFiles")
        if program_files:
            candidates.append(Path(program_files) / "Android" / "Sdk" / "platform-tools" / "adb.exe")
        program_files_x86 = os.environ.get("ProgramFiles(x86)")
        if program_files_x86:
            candidates.append(Path(program_files_x86) / "Android" / "android-sdk" / "platform-tools" / "adb.exe")
        for candidate in candidates:
            if candidate.is_file():
                return str(candidate)

        return "adb"

    def _hidden_subprocess_options(self) -> dict:
        if os.name != "nt":
            return {}

        startupinfo = subprocess.STARTUPINFO()
        startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startupinfo.wShowWindow = subprocess.SW_HIDE
        return {
            "startupinfo": startupinfo,
            "creationflags": subprocess.CREATE_NO_WINDOW,
        }

    def run(self, args: Iterable[str], timeout: float = 8.0) -> Tuple[int, str, str]:
        command = [self.adb_path, *args]
        try:
            with self._run_lock:
                completed = subprocess.run(
                    command,
                    capture_output=True,
                    text=True,
                    encoding="utf-8",
                    errors="replace",
                    timeout=timeout,
                    check=False,
                    **self._hidden_subprocess_options(),
                )
        except FileNotFoundError:
            return 127, "", f"未找到 adb：{self.adb_path}"
        except PermissionError:
            return 126, "", f"无权限执行 adb：{self.adb_path}"
        except subprocess.TimeoutExpired:
            return 124, "", "adb 命令超时"
        except OSError as exc:
            return 125, "", f"adb 启动失败：{exc}"

        return completed.returncode, completed.stdout, completed.stderr

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

        self.sync_board_time(serial=serial)

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
                **self._hidden_subprocess_options(),
            )
        except FileNotFoundError as exc:
            raise RuntimeError(f"未找到 adb：{self.adb_path}") from exc

        time.sleep(0.5)
        if self.runtime_process.poll() is not None:
            self.runtime_process = None
            raise RuntimeError("板端运行进程立即退出")

    def sync_board_time(self, serial: Optional[str] = None) -> bool:
        utc_now = datetime.now(timezone.utc)
        date_value = utc_now.strftime("%m%d%H%M%Y.%S")
        command = f"date -u {shlex.quote(date_value)} >/dev/null 2>&1"
        code, _stdout, _stderr = self.shell(command, serial=serial, timeout=3.0)
        return code == 0

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

    def remote_file_size(self, remote_path: str, serial: Optional[str] = None, timeout: float = 2.5) -> int:
        quoted_path = shlex.quote(remote_path)
        command = (
            f"if [ -f {quoted_path} ]; then "
            f"wc -c < {quoted_path} 2>/dev/null | tr -d ' '; "
            "else echo 0; fi"
        )
        code, stdout, stderr = self.shell(command, serial=serial, timeout=timeout)
        if code != 0:
            raise RuntimeError(stderr or stdout or f"读取远端文件大小失败：{remote_path}")
        try:
            return max(0, int(stdout.strip().splitlines()[-1]))
        except (IndexError, ValueError) as exc:
            raise RuntimeError(f"远端文件大小解析失败：{remote_path}") from exc

    def tail_file(
        self,
        remote_path: str,
        offset: int,
        serial: Optional[str] = None,
        timeout: float = 3.0,
    ) -> Tuple[str, int]:
        marker = "__AICAM_TAIL_OFFSET__="
        safe_offset = max(0, int(offset))
        quoted_path = shlex.quote(remote_path)
        command = (
            f"file={quoted_path}; offset={safe_offset}; "
            "if [ ! -f \"$file\" ]; then "
            f"printf '\\n{marker}0\\n'; exit 0; "
            "fi; "
            "size=$(wc -c < \"$file\" 2>/dev/null | tr -d ' '); "
            "case \"$size\" in ''|*[!0-9]*) size=0;; esac; "
            "if [ \"$size\" -lt \"$offset\" ]; then offset=0; fi; "
            "if [ \"$size\" -gt \"$offset\" ]; then "
            "start=$((offset + 1)); tail -c +\"$start\" \"$file\" 2>/dev/null; "
            "fi; "
            f"printf '\\n{marker}%s\\n' \"$size\""
        )
        code, stdout, stderr = self.shell(command, serial=serial, timeout=timeout)
        if code != 0:
            raise RuntimeError(stderr or stdout or f"读取远端日志失败：{remote_path}")

        text = stdout.replace("\r\n", "\n")
        marker_index = text.rfind("\n" + marker)
        if marker_index < 0 and text.startswith(marker):
            marker_index = 0
        if marker_index < 0:
            raise RuntimeError(f"远端日志响应缺少 offset 标记：{remote_path}")

        payload = text[:marker_index]
        marker_line = text[marker_index:].lstrip("\n").splitlines()[0]
        try:
            new_offset = max(0, int(marker_line[len(marker):].strip()))
        except ValueError as exc:
            raise RuntimeError(f"远端日志 offset 解析失败：{remote_path}") from exc

        return payload, new_offset

    def pull_file(
        self,
        remote_path: str,
        local_path: Path,
        serial: Optional[str] = None,
        timeout: float = 4.0,
    ) -> bool:
        local_path.parent.mkdir(parents=True, exist_ok=True)
        args = []
        if serial:
            args.extend(["-s", serial])
        args.extend(["pull", remote_path, str(local_path)])
        code, _stdout, _stderr = self.run(args, timeout=timeout)
        return code == 0 and local_path.exists()

    def pull_logs(
        self,
        remote_log_dir: str,
        local_log_dir: Path,
        serial: Optional[str] = None,
        timeout: float = 4.0,
    ) -> Tuple[bool, bool]:
        events_ok = self.pull_file(
            f"{remote_log_dir.rstrip('/')}/events.jsonl",
            local_log_dir / "events.jsonl",
            serial=serial,
            timeout=timeout,
        )
        metrics_ok = self.pull_file(
            f"{remote_log_dir.rstrip('/')}/metrics.jsonl",
            local_log_dir / "metrics.jsonl",
            serial=serial,
            timeout=timeout,
        )
        return events_ok, metrics_ok
