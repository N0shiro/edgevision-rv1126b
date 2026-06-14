# AICAM Dashboard

PySide6 desktop dashboard for the RV1126B AICAM runtime.

## Features

- ADB USB mode with one-click board runtime start and automatic `adb forward tcp:18080 tcp:8080`
- LAN URL mode for direct `http://<board-ip>:8080/` playback
- Offline log mode for local `events.jsonl` and `metrics.jsonl`
- Real-time video display through OpenCV
- Detection event table
- Per-label count summary
- FPS, CPU, memory and inference-time charts
- CSV export for detection events

The dashboard intentionally uses JSONL files directly instead of a database.
This keeps the edge-side runtime lightweight and makes offline demos easy.

## Install

```powershell
cd <repo-root>\qt_dashboard
python -m venv .venv
.\.venv\Scripts\activate
python -m pip install --upgrade pip
pip install -r requirements.txt
```

If PowerShell blocks activation scripts, run this once in the current shell:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\.venv\Scripts\activate
```

`adb` must be available in `PATH`. If not, set:

```powershell
$env:AICAM_ADB = "C:\path\to\adb.exe"
```

## Run

```powershell
python main.py
```

## ADB USB Mode

1. Deploy the runtime package to `/userdata/aicam` on the board.
2. Connect the board to the PC with USB.
3. Confirm `adb devices` shows one online device.
4. Open the dashboard.
5. Select `ADB USB`.
6. Keep `Board runtime` as `/userdata/aicam`, unless you deployed elsewhere.
7. Click `Start Runtime`.

The dashboard maps:

```text
PC 127.0.0.1:18080 -> board 127.0.0.1:8080
```

It starts the board process with:

```text
cd /userdata/aicam && bash ./start_aicam.sh > logs/startup.out 2>&1
```

It also pulls:

```text
/userdata/aicam/logs/events.jsonl
/userdata/aicam/logs/metrics.jsonl
```

to:

```text
qt_dashboard/runtime/logs/
```

Click `Stop Runtime` to stop the local dashboard workers and kill board-side `camera` / `camera_gateway`.

## LAN URL Mode

Use this when the board and PC are on the same network:

```text
http://<board-ip>:8080/
```

## Offline Logs Mode

Use local JSONL files for demo fallback. The default local paths are:

```text
qt_dashboard/runtime/logs/events.jsonl
qt_dashboard/runtime/logs/metrics.jsonl
```

You can browse and select files from `sample_data/` or any local directory.

## Package Later

```powershell
pyinstaller -F -w main.py --name AICAM-Dashboard
```
