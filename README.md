# edgevision-rv1126b 边缘智能视频监控系统

`edgevision-rv1126b` 是一个面向 Rockchip `RV1126B` 平台的边缘智能视频监控课设项目。系统在板端完成摄像头采集、H.264 硬件编码、HTTP 视频流分发、RKNN 目标检测、检测事件记录和运行指标采集；在 Windows 主机端提供 Qt 上位机 GUI，用于一键通过 ADB USB 启动板端运行包、自动端口转发、播放实时视频流，并显示 `events.jsonl` / `metrics.jsonl`。

当前分支：`feature/qt-dashboard-mvp`。

## 项目目标

本项目要解决的问题是：在资源受限的嵌入式 AI 摄像头平台上，将视频采集、硬件编码、端侧目标检测、事件记录和可视化调试打通成一个完整闭环。最终验收时，操作者只需要在 Windows 上打开上位机，选择 ADB USB 设备并点击启动，就可以看到实时视频、检测结果、类别统计和资源曲线。

可以作为课设报告摘要使用的描述：

> 本项目基于 RV1126B 嵌入式平台实现边缘智能视频监控系统。板端使用 V4L2 采集摄像头 NV12 图像，调用 Rockchip VENC 进行 H.264 硬件编码，并通过自研 `camera_gateway` 提供 HTTP 拉流服务。AI 侧集成 RKNN Runtime，使用 YOLO 风格目标检测模型完成端侧推理，并将检测结果叠加到视频画面，同时写入 JSONL 事件日志。系统还采集采集帧率、编码帧率、AI 帧率、CPU、内存、队列丢帧等运行指标。PC 端实现 PySide6 Qt 上位机，支持 ADB USB 一键启动板端、自动 `adb forward`、实时视频播放、事件表格、类别统计、资源曲线和 CSV 导出，便于演示、调试和验收。

## 当前分支成果

- 板端运行包目录约定为 `/userdata/aicam`。
- 板端程序包括 `camera`、`camera_gateway`、`start_aicam.sh`、配置文件、模型和标签文件。
- Windows 上位机位于 `qt_dashboard/`，界面已改为中文。
- GUI 支持三种模式：`ADB USB`、`局域网 URL`、`离线日志`。
- ADB USB 模式支持：
  - 自动选择或刷新 ADB 设备；
  - 一键执行板端 `/userdata/aicam/start_aicam.sh`；
  - 自动端口转发 `tcp:18080 -> tcp:8080`；
  - 自动播放 `http://127.0.0.1:18080/`；
  - 定时拉取 `/userdata/aicam/logs/events.jsonl`；
  - 定时拉取 `/userdata/aicam/logs/metrics.jsonl`。
- GUI 采用参考 B 站视频页的布局：
  - 顶部横向放置标题、连接地址、模式、设备选择和启动按钮；
  - 左侧大面积 16:9 视频播放区；
  - 右侧任务栏放置检测事件、类别统计、参数和日志导出；
  - 底部保留资源曲线，便于观察 FPS、CPU、内存和推理耗时。
- 已提供 Windows 单文件可执行程序：`qt_dashboard/dist/AICAM-Dashboard.exe`。
- `.exe` 通过 Git LFS 管理，克隆后如果只看到 LFS pointer，需要执行 `git lfs pull`。

## 系统功能

### 板端功能

- 摄像头采集：通过 `V4L2 + mmap` 采集摄像头帧。
- 图像格式：默认处理 `NV12`。
- 硬件编码：调用 Rockchip `VENC` 输出 H.264。
- 视频分发：`camera_gateway` 将 H.264 流封装为 HTTP 拉流接口。
- AI 推理：通过 `RKNN Runtime` 加载 `.rknn` 模型。
- 预处理：优先使用 Rockchip `RGA`，不可用时可回退 CPU。
- 抽帧推理：通过 `AICAM_INFER_EVERY_N` 控制 AI 负载。
- 画面叠框：检测结果可在编码前叠加到视频帧。
- 事件输出：检测结果写入 `events.jsonl`。
- 指标输出：FPS、CPU、RSS、推理耗时、队列丢帧等写入 `metrics.jsonl`。
- 安全降级：模型或运行库不可用时，视频链路仍可继续运行。

### Windows 上位机功能

- ADB 设备刷新和连接状态提示。
- ADB USB 一键启动板端运行包。
- 自动建立 `adb forward tcp:18080 tcp:8080`。
- OpenCV 读取 HTTP H.264 视频流并显示在 Qt 窗口中。
- 16:9 视频框自适应窗口大小，避免画面比例异常。
- 检测事件表格实时追加。
- 检测类别统计。
- 资源曲线显示采集帧率、编码帧率、AI 帧率、CPU、内存、推理耗时。
- 本地 CSV 导出检测事件。
- 支持离线读取本地 `events.jsonl` / `metrics.jsonl` 做演示。

## 总体架构

```text
RV1126B 板端

Camera Sensor
  |
  v
V4L2 Capture
  |
  v
Frame Queue
  |
  +--> Encode Thread
  |      |
  |      +--> Detection Overlay
  |      |
  |      +--> Rockchip VENC H.264
  |      |
  |      +--> TCP push to camera_gateway
  |
  +--> AI Thread
  |      |
  |      +--> RGA/CPU Preprocess
  |      |
  |      +--> RKNN Inference
  |      |
  |      +--> Postprocess / NMS
  |      |
  |      +--> events.jsonl
  |
  +--> Metrics Thread
         |
         +--> metrics.jsonl

camera_gateway
  |
  +--> HTTP H.264 stream: http://board:8080/

Windows PC
  |
  +--> Qt Dashboard
        |
        +--> adb shell start_aicam.sh
        +--> adb forward tcp:18080 tcp:8080
        +--> OpenCV video playback
        +--> adb pull events.jsonl / metrics.jsonl
        +--> event table / summary / resource charts
```

## 目录结构

```text
edgevision-rv1126b/
|-- ai/                    # RKNN 推理、预处理、后处理
|-- camera/                # 主采集进程：采集、编码、AI 调度、事件和指标
|-- camera_gateway/        # HTTP/H.264 网关，负责对外分发视频流
|-- capture/               # 采集帧结构、线程安全帧队列
|-- cmake/                 # RV1126B 交叉编译 toolchain
|-- common/                # 文件、时间等通用工具
|-- config/                # 板端环境变量示例
|-- docs/                  # 部署文档、测试记录等
|-- encode/                # 编码相关共享类型
|-- event/                 # 检测事件 JSONL 写入
|-- metrics/               # 运行指标采集和 JSONL 写入
|-- models/                # 标签文件和模型说明，实际 RKNN 模型通常单独部署
|-- qt_dashboard/          # Windows/PySide6 上位机 GUI
|-- scripts/               # 模型转换、运行包打包、启动脚本
|-- stream/                # H.264 TCP 推流客户端
|-- CMakeLists.txt         # 顶层 CMake 构建入口
|-- README.md              # 当前说明文档
```

## 核心模块说明

| 模块 | 关键文件 | 职责 |
| --- | --- | --- |
| 配置加载 | `camera/src/AppConfig.cpp` | 从 `AICAM_*` 环境变量读取视频、编码、AI、日志路径等配置 |
| 摄像头采集 | `camera/src/CameraDevice.cpp` | 打开 V4L2 设备，申请 mmap buffer，输出 NV12 帧 |
| ISP 控制 | `camera/src/IspController.cpp` | 初始化 Rockchip ISP 相关能力 |
| H.264 编码 | `camera/src/H264Encoder.cpp` | 调用 Rockchip VENC 编码视频帧 |
| 检测叠框 | `camera/src/DetectionOverlay.cpp` | 将 AI 检测框叠加到编码前帧 |
| 主流程 | `camera/src/main.cpp` | 管理采集、编码、AI、指标线程生命周期 |
| 网关服务 | `camera_gateway/src/TcpServer.cpp` | 接收 camera 推流并提供 HTTP 拉流 |
| RKNN 推理 | `ai/src/RknnDetector.cpp` | 加载 RKNN 模型，完成预处理、推理、后处理 |
| 事件日志 | `event/src/EventLogger.cpp` | 将每帧检测结果写为 JSONL |
| 指标日志 | `metrics/src/MetricsCollector.cpp` | 采样 FPS、CPU、RSS、推理耗时和丢帧计数 |
| 上位机 | `qt_dashboard/main.py` | GUI 主窗口、模式切换、启动停止、数据展示 |
| ADB 管理 | `qt_dashboard/services/adb_manager.py` | 封装 `adb devices`、`adb shell`、`adb forward`、`adb pull` |
| 视频播放 | `qt_dashboard/services/video_worker.py` | 后台线程用 OpenCV 拉取视频帧 |
| 日志读取 | `qt_dashboard/services/log_watcher.py` | 监听本地 JSONL 文件增量 |

## 运行环境

### 板端环境

- 硬件：RV1126B 开发板。
- 摄像头：默认 `/dev/video13`。
- 系统目录：默认运行包放在 `/userdata/aicam`。
- 运行库：
  - Rockchip VENC 相关库；
  - RKNN Runtime：`librknnrt.so`；
  - RGA：`librga.so`，用于 AI 预处理加速；
  - 具体库位置依赖 SDK 和系统镜像。

### Windows 主机环境

- Windows 10/11。
- Python 推荐 3.9+ 或 3.10+。
- ADB 可用，`adb devices` 能看到 RV1126B。
- 如果从源码运行 GUI，需要安装：
  - `PySide6`
  - `opencv-python`
  - `pyqtgraph`
  - `numpy`
- 如果直接运行 `qt_dashboard/dist/AICAM-Dashboard.exe`，通常不需要额外安装 Python GUI 依赖。
- 当前正式 exe 已内置 `adb.exe` 和 ADB 运行 DLL；新 PC 不需要单独安装 Android platform-tools，但仍需要 Windows USB 驱动能识别板端 ADB 设备。

## 克隆当前分支

```powershell
git clone git@github.com:N0shiro/edgevision-rv1126b.git
cd edgevision-rv1126b
git checkout feature/qt-dashboard-mvp
git lfs pull
```

如果本机没有配置 SSH key，也可以使用 HTTPS：

```powershell
git clone https://github.com/N0shiro/edgevision-rv1126b.git
cd edgevision-rv1126b
git checkout feature/qt-dashboard-mvp
git lfs pull
```

## 板端运行包约定

GUI 默认认为板端运行包已经部署在：

```text
/userdata/aicam
```

该目录至少应包含：

```text
/userdata/aicam/camera
/userdata/aicam/camera_gateway
/userdata/aicam/start_aicam.sh
/userdata/aicam/config/aicam.env.example
/userdata/aicam/models/yolov5n_fp.rknn
/userdata/aicam/models/coco_80_labels.txt
```

使用 ADB 检查：

```powershell
adb devices
adb shell "ls -l /userdata/aicam"
adb shell "ls -l /userdata/aicam/models"
adb shell "grep -n 'AICAM_AI_ENABLE\|AICAM_RKNN_MODEL\|AICAM_INFER_EVERY_N\|AICAM_PREPROCESS_BACKEND' /userdata/aicam/config/aicam.env.example"
```

期望关键配置类似：

```text
export AICAM_AI_ENABLE=1
export AICAM_RKNN_MODEL=/userdata/aicam/models/yolov5n_fp.rknn
export AICAM_LABELS=/userdata/aicam/models/coco_80_labels.txt
export AICAM_INFER_EVERY_N=7
export AICAM_PREPROCESS_BACKEND=rga
```

## 交叉编译

### 顶层一次性构建

```bash
cd <repo-root>
export SDK_ROOT=<sdk-root>

cmake -S . -B build-aicam-check \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchains/rv1126b-aarch64-linux-gnu.cmake \
  -DTOOLCHAIN_PREFIX=aarch64-rockchip1240-linux-gnu \
  -DTOOLCHAIN_BIN_DIR=$SDK_ROOT/tools/linux/toolchain/aarch64-rockchip1240-linux-gnu/bin \
  -DCMAKE_SYSROOT=$SDK_ROOT/tools/linux/toolchain/aarch64-rockchip1240-linux-gnu/aarch64-rockchip1240-linux-gnu/sysroot \
  -DROCKIT_ROOT=$SDK_ROOT/output/out/media_out

cmake --build build-aicam-check -j4
```

### 只构建 camera 模块

```bash
cd <repo-root>
export SDK_ROOT=<sdk-root>

cmake -S camera -B camera/build-aicam-check \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchains/rv1126b-aarch64-linux-gnu.cmake \
  -DTOOLCHAIN_PREFIX=aarch64-rockchip1240-linux-gnu \
  -DTOOLCHAIN_BIN_DIR=$SDK_ROOT/tools/linux/toolchain/aarch64-rockchip1240-linux-gnu/bin \
  -DCMAKE_SYSROOT=$SDK_ROOT/tools/linux/toolchain/aarch64-rockchip1240-linux-gnu/aarch64-rockchip1240-linux-gnu/sysroot \
  -DROCKIT_ROOT=$SDK_ROOT/output/out/media_out

cmake --build camera/build-aicam-check -j4
```

### 打包板端运行目录

顶层构建完成后执行：

```bash
cd <repo-root>
bash scripts/package_runtime.sh
```

脚本默认从 `build-aicam-check` 收集可执行文件，并生成 `dist-aicam/`。如需指定库路径：

```bash
export AICAM_RKNNRT_LIB=<path-to-librknnrt.so>
export AICAM_RGA_LIB=<path-to-librga.so>
export AICAM_RKAIQ_LIB=<path-to-rkaiq-lib>
bash scripts/package_runtime.sh
```

部署到板端示例：

```bash
adb shell "mkdir -p /userdata/aicam"
adb push dist-aicam/. /userdata/aicam/
adb shell "chmod +x /userdata/aicam/camera /userdata/aicam/camera_gateway /userdata/aicam/start_aicam.sh"
```

## 板端手动启动

在板端或通过 ADB 执行：

```bash
cd /userdata/aicam
bash ./start_aicam.sh
```

`start_aicam.sh` 会：

1. 加载 `config/aicam.env.example`。
2. 创建 `logs/` 目录。
3. 停止旧的 `camera` / `camera_gateway` / `rkipc`。
4. 先启动 `camera_gateway`。
5. 再启动 `camera`。
6. 将启动日志写入 `logs/startup.out`。

常用检查命令：

```powershell
adb shell "ps | grep -E 'camera|camera_gateway' | grep -v grep"
adb shell "tail -n 80 /userdata/aicam/logs/startup.out"
adb shell "tail -n 5 /userdata/aicam/logs/metrics.jsonl"
adb shell "tail -n 5 /userdata/aicam/logs/events.jsonl"
```

## Windows GUI 使用方式

### 方式一：直接运行 exe

当前分支已提交：

```text
qt_dashboard/dist/AICAM-Dashboard.exe
```

双击或在 PowerShell 中运行：

```powershell
cd <repo-root>\qt_dashboard\dist
.\AICAM-Dashboard.exe
```

说明：

- 这是 PyInstaller 单文件程序，首次启动需要解压运行环境，可能比源码启动慢。
- 正式 exe 已内置 ADB 工具，属于 Qt + OpenCV + NumPy + Python + ADB 的单文件打包。
- 该 exe 已通过 Git LFS 提交，克隆后要确保 `git lfs pull` 已执行。

### 方式二：从源码运行

进入 GUI 目录：

```powershell
cd <repo-root>\qt_dashboard
```

使用 venv：

```powershell
python -m venv .venv
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\.venv\Scripts\activate
python -m pip install --upgrade pip
pip install -r requirements.txt
python main.py
```

使用 conda：

```powershell
conda create -n aicam-dashboard python=3.10 -y
conda activate aicam-dashboard
cd <repo-root>\qt_dashboard
python -m pip install --upgrade pip
pip install -r requirements.txt
python main.py
```

如果本机已有 conda 环境，也可以直接激活已有环境后安装缺失依赖：

```powershell
conda activate <your-env>
cd <repo-root>\qt_dashboard
pip install -r requirements.txt
python main.py
```

### ADB 路径配置

先检查：

```powershell
python --version
adb version
adb devices
```

如果 `adb` 不在 `PATH`，设置环境变量：

```powershell
$env:AICAM_ADB="C:\path\to\adb.exe"
```

例如 Android SDK 常见路径：

```powershell
$env:AICAM_ADB="$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
```

## GUI 操作流程

1. 用 USB 连接 RV1126B。
2. 在 PowerShell 确认：

```powershell
adb devices
```

3. 打开 `AICAM-Dashboard.exe` 或执行 `python main.py`。
4. 顶部 `模式` 选择 `ADB USB`。
5. 顶部 `设备` 选择在线设备，例如 `600226ef96e6051b`。
6. `链接` 默认显示 `http://127.0.0.1:18080/`，ADB 模式下该地址为只读。
7. 点击 `连接 ADB`，GUI 会建立：

```text
adb forward tcp:18080 tcp:8080
```

8. 点击 `启动板端`，GUI 会执行类似命令：

```text
adb shell "cd /userdata/aicam && bash ./start_aicam.sh > logs/startup.out 2>&1 &"
```

9. GUI 开始播放：

```text
http://127.0.0.1:18080/
```

10. GUI 定时拉取并显示：

```text
/userdata/aicam/logs/events.jsonl
/userdata/aicam/logs/metrics.jsonl
```

11. 右侧 `参数 / 日志` 标签页可查看或修改：
    - 板端运行目录；
    - 板端日志目录；
    - 本地事件日志路径；
    - 本地性能日志路径；
    - 手动拉取日志；
    - 导出 CSV；
    - 清空当前 GUI 视图。

12. 点击 `停止板端` 会停止本地视频/日志线程，并通过 ADB 尝试停止板端 `camera` 和 `camera_gateway`。

## GUI 布局说明

当前 GUI 采用“视频页”布局，目的是把主要空间让给视频画面：

- 顶部工具栏：系统标题、连接地址、运行模式、ADB 设备、刷新、连接、启动、停止。
- 中央左侧：16:9 视频播放区。
- 中央右侧：任务栏标签页。
  - `检测事件`：实时展示检测到的目标、置信度、坐标和推理耗时。
  - `类别统计`：按类别累计检测数量。
  - `参数 / 日志`：运行路径、日志路径、拉取日志、导出 CSV。
- 底部：资源曲线，显示 FPS、CPU、内存和推理耗时。

这种布局适合验收演示：评委或报告阅读者首先看到视频主体，其次能看到检测事件和资源占用，配置项和导出按钮不会挤占视频区域。

## 视频流验证

GUI 内部用 OpenCV 读取视频流。如果 GUI 没有画面，可以先确认端口转发：

```powershell
adb forward --list
```

预期能看到类似：

```text
600226ef96e6051b tcp:18080 tcp:8080
```

本地也可以用 `ffplay` 验证：

```powershell
ffplay -fflags nobuffer -flags low_delay -framedrop -sync video -probesize 32 -analyzeduration 0 http://127.0.0.1:18080/
```

如果没有 `ffplay`，可用 VLC 打开：

```text
http://127.0.0.1:18080/
```

也可以用 Python + OpenCV 做最小验证：

```powershell
@'
import cv2
cap = cv2.VideoCapture("http://127.0.0.1:18080/")
ok, frame = cap.read()
print("ok=", ok, "shape=", None if frame is None else frame.shape)
cap.release()
'@ | python -
```

## 日志格式

### events.jsonl

`events.jsonl` 每一行是一条 JSON。每条记录对应一次 AI 推理结果，可能包含多个 detection。

示例：

```json
{"timestamp":"2026-06-15 12:00:00.000000","frame_sequence":1234,"backend":"rknn","preprocess_ms":2.1,"inference_ms":38.5,"postprocess_ms":1.3,"note":"","detections":[{"class_id":0,"label":"person","score":0.82,"x1":120,"y1":96,"x2":420,"y2":720}]}
```

主要字段：

| 字段 | 含义 |
| --- | --- |
| `timestamp` | 采集时间 |
| `frame_sequence` | 帧序号 |
| `backend` | 推理后端，例如 `rknn` |
| `preprocess_ms` | 预处理耗时 |
| `inference_ms` | RKNN 推理耗时 |
| `postprocess_ms` | 后处理耗时 |
| `detections` | 检测框数组 |
| `class_id` | 类别 ID |
| `label` | 类别名称 |
| `score` | 置信度 |
| `x1,y1,x2,y2` | 检测框坐标 |

### metrics.jsonl

`metrics.jsonl` 每一行是一条运行指标采样。

示例：

```json
{"timestamp":"2026-06-15 12:00:05.000000","capture_fps":30.0,"encode_fps":30.0,"ai_fps":4.3,"average_encode_ms":4.1,"average_inference_ms":39.2,"cpu_percent":91.5,"rss_mb":80.4,"bytes_sent":12345678,"events_written":128,"encode_queue_drops":0,"ai_queue_drops":0}
```

主要字段：

| 字段 | 含义 |
| --- | --- |
| `capture_fps` | 摄像头采集帧率 |
| `encode_fps` | H.264 编码帧率 |
| `ai_fps` | AI 推理帧率 |
| `average_encode_ms` | 平均编码耗时 |
| `average_inference_ms` | 平均推理耗时 |
| `cpu_percent` | `camera` 进程 CPU 占用 |
| `rss_mb` | `camera` 进程常驻内存 |
| `bytes_sent` | 已发送视频字节数 |
| `events_written` | 已写入事件数量 |
| `encode_queue_drops` | 编码队列丢帧 |
| `ai_queue_drops` | AI 队列丢帧 |

## 关键配置项

配置文件：

```text
config/aicam.env.example
```

板端运行时默认加载：

```text
/userdata/aicam/config/aicam.env.example
```

常用变量：

| 变量 | 默认/示例 | 说明 |
| --- | --- | --- |
| `AICAM_GATEWAY_IP` | `127.0.0.1` | `camera_gateway` 监听地址 |
| `AICAM_GATEWAY_PORT` | `8080` | HTTP 视频流端口 |
| `AICAM_VIDEO_DEVICE` | `/dev/video13` | 摄像头设备节点 |
| `AICAM_WIDTH` | `1920` | 采集宽度 |
| `AICAM_HEIGHT` | `1080` | 采集高度 |
| `AICAM_FPS` | `30` | 采集帧率 |
| `AICAM_BITRATE_KBPS` | `8192` | H.264 码率 |
| `AICAM_GOP` | `30` | GOP 长度 |
| `AICAM_ENCODE_QUEUE` | `5` | 编码队列容量 |
| `AICAM_AI_QUEUE` | `3` | AI 队列容量 |
| `AICAM_OVERLAY_ENABLE` | `1` | 是否叠加检测框 |
| `AICAM_AI_ENABLE` | `1` | 是否启用 AI |
| `AICAM_RKNN_MODEL` | `/userdata/aicam/models/yolov5n_fp.rknn` | RKNN 模型路径 |
| `AICAM_LABELS` | `/userdata/aicam/models/coco_80_labels.txt` | 标签文件路径 |
| `AICAM_INFER_EVERY_N` | `7` | 每 N 帧推理一次 |
| `AICAM_PREPROCESS_BACKEND` | `rga` | 预处理后端，`rga` 或 `cpu` |
| `AICAM_SCORE_THRESHOLD` | `0.35` | 检测置信度阈值 |
| `AICAM_NMS_THRESHOLD` | `0.45` | NMS 阈值 |
| `AICAM_MAX_RESULTS` | `20` | 单帧最大检测数 |
| `AICAM_BOX_FORMAT` | `xywh` | 模型输出框格式 |

## RKNN 模型说明

当前 `RknnDetector` 主要面向 YOLO 风格检测模型。支持的常见输出布局包括：

```text
[N, 6]
[1, N, 6]
[1, N, 85]
[1, 84, N]
```

建议使用轻量检测模型，并尽量选择方便端侧后处理的输出格式。当前分支默认运行包期望：

```text
/userdata/aicam/models/yolov5n_fp.rknn
/userdata/aicam/models/coco_80_labels.txt
```

如果需要自行转换模型，可参考：

```bash
python scripts/convert_to_rknn.py \
  --onnx <model.onnx> \
  --output <model.rknn> \
  --dataset <dataset.txt>
```

具体参数以脚本 `--help` 为准。

## AI 启用状态检查

启动后查看：

```powershell
adb shell "tail -n 120 /userdata/aicam/logs/startup.out"
```

AI 正常启用时应看到类似：

```text
ai_enabled=true
RKNN detector ready
preprocess=rga(rga_available)
```

如果看到：

```text
ai_enabled=false
```

重点检查：

1. GUI 或命令行是否用 `bash ./start_aicam.sh` 启动。
2. `/userdata/aicam/config/aicam.env.example` 是否存在且被加载。
3. `AICAM_AI_ENABLE=1` 是否设置。
4. `AICAM_RKNN_MODEL` 指向的 `.rknn` 文件是否存在。
5. `librknnrt.so` 是否在运行包或系统库路径中。
6. 模型输入输出布局是否被当前 `RknnDetector` 支持。

## 性能预期

在当前默认配置下，本次实测目标接近：

| 指标 | 预期范围 |
| --- | --- |
| `capture_fps` | 约 30 |
| `encode_fps` | 约 30 |
| `ai_fps` | 约 4.2 到 4.4 |
| `cpu_percent` | 约 90% 到 92% |
| `encode_queue_drops` | 0 |
| `ai_queue_drops` | 0 |

影响性能的主要因素：

- 摄像头分辨率和帧率；
- H.264 码率；
- 模型大小和输出头复杂度；
- `AICAM_INFER_EVERY_N` 抽帧间隔；
- 预处理是否走 RGA；
- 是否开启叠框；
- 板端系统负载和散热状态。

如果 AI 占用过高，可以优先调大：

```bash
export AICAM_INFER_EVERY_N=10
```

如果希望检测更及时，可以调小该值，但 CPU/NPU/内存压力会增加。

## 常见问题排查

### 1. ADB 找不到

现象：

```text
adb: The term 'adb' is not recognized
```

处理：

```powershell
$env:AICAM_ADB="C:\path\to\adb.exe"
adb version
```

也可以把 `adb.exe` 所在目录加入系统 `PATH`。

### 2. 多个 ADB 设备导致启动失败

检查：

```powershell
adb devices
```

如果有多个设备或模拟器，在 GUI 顶部 `设备` 下拉框中选择 RV1126B 的真实序列号，例如：

```text
600226ef96e6051b
```

### 3. GUI 没有画面

依次检查：

```powershell
adb devices
adb forward --list
adb shell "ps | grep -E 'camera|camera_gateway' | grep -v grep"
adb shell "tail -n 80 /userdata/aicam/logs/startup.out"
```

如果没有端口转发，可手动执行：

```powershell
adb forward tcp:18080 tcp:8080
```

如果仍无画面，用 `ffplay` 或 VLC 打开：

```text
http://127.0.0.1:18080/
```

### 4. 有视频但没有检测事件

检查：

```powershell
adb shell "tail -n 120 /userdata/aicam/logs/startup.out"
adb shell "tail -n 5 /userdata/aicam/logs/events.jsonl"
```

关注：

```text
ai_enabled=true
RKNN detector ready
```

如果模型加载失败，`startup.out` 会给出 RKNN 初始化或输出解析相关错误。

### 5. metrics.jsonl 不更新

检查进程是否存在：

```powershell
adb shell "ps | grep -E 'camera|camera_gateway' | grep -v grep"
```

检查文件权限和路径：

```powershell
adb shell "ls -l /userdata/aicam/logs"
adb shell "tail -n 20 /userdata/aicam/logs/startup.out"
```

### 6. GUI 显示日志拉取失败

常见原因：

- 板端 `logs/` 目录尚未生成；
- `camera` 没有启动成功；
- ADB 设备离线；
- 板端运行目录不是 `/userdata/aicam`。

可在右侧 `参数 / 日志` 中确认板端运行目录和板端日志目录。

### 7. exe 启动慢或体积大

原因：

- PyInstaller 单文件模式会把 Python、Qt、OpenCV、NumPy、pyqtgraph 等依赖打入一个 exe。
- 首次启动会解压运行时文件。

当前 exe 约 106 MiB。去掉资源曲线只能减少少量体积，因为主要体积来自 Qt、OpenCV 和 NumPy。

## 打包 Windows exe

进入 GUI 目录：

```powershell
cd <repo-root>\qt_dashboard
```

安装依赖：

```powershell
pip install -r requirements.txt
pip install pyinstaller
```

基础打包：

```powershell
pyinstaller -F -w main.py --name AICAM-Dashboard
```

当前分支已使用更偏向体积控制的 PyInstaller 配置打包，并将产物放在：

```text
qt_dashboard/dist/AICAM-Dashboard.exe
```

注意：

- `qt_dashboard/dist/` 默认在 `.gitignore` 中被忽略。
- 当前 `.gitattributes` 已对 `qt_dashboard/dist/*.exe` 开启 Git LFS。
- 如果重新打包并需要提交 exe，需要显式添加：

```powershell
git add -f qt_dashboard/dist/AICAM-Dashboard.exe
```

## 报告撰写建议

可以按以下结构撰写课设报告。

### 1. 项目背景

- 嵌入式设备直接处理视频可以减少网络传输压力。
- 边缘 AI 能降低延迟，适合实时监控、智能识别、告警等场景。
- RV1126B 提供视频编解码、RGA、NPU 等能力，适合做端侧视频 AI。

### 2. 需求分析

- 摄像头实时采集。
- H.264 硬件编码和网络拉流。
- 目标检测模型端侧推理。
- 检测事件结构化记录。
- 运行状态可观测。
- Windows 上位机一键启动和可视化。
- 支持异常排查和离线演示。

### 3. 总体设计

重点说明：

- 板端负责实时链路和 AI。
- 主机端负责控制、展示和导出。
- 使用 JSONL 代替数据库，降低板端复杂度。
- ADB USB 用于开发调试，局域网 URL 用于网络部署。

### 4. 详细设计

可分模块写：

- 采集模块；
- 编码模块；
- 网关模块；
- AI 推理模块；
- 事件日志模块；
- 指标采集模块；
- Qt 上位机模块。

### 5. 实现细节

建议重点写：

- V4L2 mmap 采集流程；
- VENC 编码流程；
- RKNN 初始化、输入输出查询、推理调用；
- RGA 预处理和 CPU 回退；
- JSONL 数据格式；
- ADB 一键启动和端口转发；
- GUI 的线程模型，视频读取和日志监听放在后台线程，避免阻塞 UI。

### 6. 测试与结果

建议展示：

- `adb devices` 设备在线截图；
- `/userdata/aicam` 目录截图；
- GUI 主界面截图；
- 视频播放截图；
- `events.jsonl` 示例；
- `metrics.jsonl` 示例；
- 资源曲线截图；
- 性能数据表。

可使用本 README 的性能预期表作为报告数据基础，再结合实际运行截图补充。

### 7. 问题与改进

可写：

- 单文件 exe 体积较大；
- HTTP 裸 H.264 对播放器兼容性不如 RTSP/WebRTC；
- 目前检测后处理主要面向 YOLO 风格输出；
- 当前只保存 JSONL，没有数据库和 Web 后台；
- 后续可增加截图留存、事件检索、告警推送、多路摄像头、模型热切换。

## 已知限制

- 网关当前输出为 `HTTP + H.264`，不是 RTSP 或 WebRTC。
- 默认摄像头节点为 `/dev/video13`，不同板卡或镜像可能需要修改。
- 当前 AI 后处理优先支持 YOLO 风格检测输出，不覆盖所有模型结构。
- 模型文件通常较大，不一定直接纳入 Git，需要单独部署到板端。
- Windows exe 体积较大，这是 Qt/OpenCV/Python 单文件分发的正常代价。
- GUI 主要用于开发调试和课设演示，不是完整生产级运维平台。

## 快速命令清单

### Windows 主机

```powershell
git clone git@github.com:N0shiro/edgevision-rv1126b.git
cd edgevision-rv1126b
git checkout feature/qt-dashboard-mvp
git lfs pull

adb version
adb devices

cd qt_dashboard\dist
.\AICAM-Dashboard.exe
```

### 源码运行 GUI

```powershell
cd <repo-root>\qt_dashboard
python -m venv .venv
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\.venv\Scripts\activate
python -m pip install --upgrade pip
pip install -r requirements.txt
python main.py
```

### 板端检查

```powershell
adb shell "ls -l /userdata/aicam"
adb shell "ls -l /userdata/aicam/models"
adb shell "cd /userdata/aicam && bash ./start_aicam.sh"
adb shell "tail -n 80 /userdata/aicam/logs/startup.out"
adb shell "tail -n 5 /userdata/aicam/logs/metrics.jsonl"
adb shell "tail -n 5 /userdata/aicam/logs/events.jsonl"
```

### 本地视频测试

```powershell
adb forward tcp:18080 tcp:8080
ffplay -fflags nobuffer -flags low_delay -framedrop -sync video -probesize 32 -analyzeduration 0 http://127.0.0.1:18080/
```

如果没有 `ffplay`，使用 VLC 打开：

```text
http://127.0.0.1:18080/
```

## 相关文档

- [板端部署说明](docs/DEPLOYMENT.md)
- [Qt 上位机说明](qt_dashboard/README.md)
