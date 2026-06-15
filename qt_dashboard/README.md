# AICAM Qt 上位机

`qt_dashboard` 是 RV1126B AICAM 运行包的 Windows/PySide6 上位机。它的主要目标是让课设验收和现场调试尽量简单：通过 ADB USB 一键启动板端 `/userdata/aicam/start_aicam.sh`，自动端口转发，播放视频流，并实时展示检测事件和性能指标。

## 功能

- `ADB USB` 模式：一键启动板端运行包，自动执行 `adb forward tcp:18080 tcp:8080`。
- `局域网 URL` 模式：直接播放 `http://<board-ip>:8080/`。
- `离线日志` 模式：读取本地 `events.jsonl` 和 `metrics.jsonl`，用于无板端演示。
- OpenCV 后台线程读取视频流，Qt 主线程显示画面。
- 视频框保持 16:9 比例。
- 检测事件表格实时追加。
- 检测类别统计。
- 底部资源曲线显示 FPS、CPU、内存和推理耗时。
- 右侧任务栏集中放置参数、日志路径、手动拉取和 CSV 导出。

## 直接运行 exe

当前分支已提供：

```text
qt_dashboard/dist/AICAM-Dashboard.exe
```

运行：

```powershell
cd <repo-root>\qt_dashboard\dist
.\AICAM-Dashboard.exe
```

说明：

- exe 是 PyInstaller 单文件包，首次启动可能较慢。
- exe 约 106 MiB，主要体积来自 Qt、OpenCV、NumPy 和 Python 运行时。
- exe 通过 Git LFS 管理，克隆仓库后如文件不完整，请在仓库根目录执行 `git lfs pull`。

## 从源码运行

```powershell
cd <repo-root>\qt_dashboard
python -m venv .venv
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\.venv\Scripts\activate
python -m pip install --upgrade pip
pip install -r requirements.txt
python main.py
```

也可以使用已有 conda 环境：

```powershell
conda activate <your-env>
cd <repo-root>\qt_dashboard
pip install -r requirements.txt
python main.py
```

依赖文件：

```text
requirements.txt
```

当前依赖：

```text
PySide6
opencv-python
pyqtgraph
numpy
```

## ADB 配置

检查 ADB：

```powershell
adb version
adb devices
```

如果 `adb` 不在 `PATH`，设置：

```powershell
$env:AICAM_ADB="C:\path\to\adb.exe"
```

GUI 的 ADB 管理逻辑会优先读取 `AICAM_ADB`，否则使用 `adb`。

## ADB USB 操作流程

1. 确保板端运行包已经部署到 `/userdata/aicam`。
2. USB 连接 RV1126B。
3. 执行 `adb devices`，确认设备状态为 `device`。
4. 打开 GUI。
5. 顶部 `模式` 选择 `ADB USB`。
6. 顶部 `设备` 选择 RV1126B 序列号。
7. 点击 `连接 ADB`。
8. 点击 `启动板端`。

GUI 会自动完成：

```text
adb shell "cd /userdata/aicam && bash ./start_aicam.sh ..."
adb forward tcp:18080 tcp:8080
打开 http://127.0.0.1:18080/
定时拉取 /userdata/aicam/logs/events.jsonl
定时拉取 /userdata/aicam/logs/metrics.jsonl
```

点击 `停止板端` 后，GUI 会停止本地视频和日志线程，并通过 ADB 尝试停止板端 `camera` / `camera_gateway`。

## 界面布局

- 顶部：标题、连接地址、模式、设备、刷新、连接、启动、停止。
- 左侧：大面积实时视频播放区，保持 16:9。
- 右侧：任务栏标签页。
  - `检测事件`
  - `类别统计`
  - `参数 / 日志`
- 底部：资源曲线。

ADB 和离线模式下，链接输入框为只读；局域网模式下可编辑 URL。

## 日志路径

板端默认：

```text
/userdata/aicam/logs/events.jsonl
/userdata/aicam/logs/metrics.jsonl
```

本地默认：

```text
qt_dashboard/runtime/logs/events.jsonl
qt_dashboard/runtime/logs/metrics.jsonl
```

`runtime/` 目录用于本地运行缓存和截图，不作为源码提交内容。

## 视频流排查

检查端口转发：

```powershell
adb forward --list
```

检查板端进程：

```powershell
adb shell "ps | grep -E 'camera|camera_gateway' | grep -v grep"
```

检查启动日志：

```powershell
adb shell "tail -n 80 /userdata/aicam/logs/startup.out"
```

本地播放器测试：

```powershell
ffplay -fflags nobuffer -flags low_delay -framedrop -sync video -probesize 32 -analyzeduration 0 http://127.0.0.1:18080/
```

如果没有 `ffplay`，用 VLC 打开：

```text
http://127.0.0.1:18080/
```

## 打包 exe

```powershell
cd <repo-root>\qt_dashboard
pip install pyinstaller
pyinstaller -F -w main.py --name AICAM-Dashboard
```

如果要提交打包产物，请在仓库根目录执行：

```powershell
git add -f qt_dashboard/dist/AICAM-Dashboard.exe
```

仓库根目录 `.gitattributes` 已配置 `qt_dashboard/dist/*.exe` 使用 Git LFS。
