# edgevision-rv1126b

edgevision-rv1126b 是一个面向 `RV1126B` 平台的边缘智能视频监控系统，集成 `V4L2` 视频采集、Rockchip `VENC` H.264 硬编码、网关分发、`RKNN` 目标检测、事件输出、性能统计与视频叠框显示，支持在开发板上完成从视频采集到端侧推理的完整部署闭环。

## 核心特性

- `V4L2 + mmap` 摄像头采集与 `NV12` 帧处理
- Rockchip `VENC` 低延迟 `H.264` 硬编码
- `camera_gateway` 提供 `HTTP + H.264` 拉流能力
- `RKNN` 板端目标检测，支持抽帧推理与安全降级
- 编码前软件叠框，播放端可直接看到检测框
- `events.jsonl` / `metrics.jsonl` 输出检测结果与运行指标
- 支持交叉编译、运行时打包、`adb` 部署与主机侧播放

## 系统架构

```text
Camera
  -> Capture(V4L2)
  -> Frame Queue
     -> Encode Thread -> Overlay -> H.264 -> Gateway -> Viewer
     -> AI Thread -> Preprocess -> RKNN Infer -> Postprocess -> Event JSON
     -> Metrics Thread -> FPS / CPU / RSS / Throughput
```

## 目录结构

```text
edgevision-rv1126b/
├─ ai/                # RKNN 推理与后处理
├─ camera/            # 主采集进程，负责采集 / 编码 / AI 调度 / 指标
├─ camera_gateway/    # epoll 网关，负责接收裸 H.264 并对外分发
├─ capture/           # 帧结构与帧队列
├─ common/            # 文件与时间等通用工具
├─ config/            # 环境变量样例
├─ docs/              # 计划书、部署说明、测试记录
├─ encode/            # 编码相关共享类型
├─ event/             # 事件日志与 JSON 输出
├─ metrics/           # 运行时指标采集与落盘
├─ models/            # 模型与标签文件目录说明
├─ scripts/           # 转模、启动、部署辅助脚本
├─ stream/            # 长度前缀 TCP 推流客户端
└─ CMakeLists.txt
```

## 关键模块

### 视频链路

- `camera/src/main.cpp`
  负责线程生命周期、配置加载、采集分发、AI/指标线程调度。
- `camera/src/CameraDevice.cpp`
  负责 `V4L2` 采集和 `NV12` 紧凑重打包。
- `camera/src/H264Encoder.cpp`
  负责 Rockchip `VENC` 编码。
- `camera/src/DetectionOverlay.cpp`
  负责在编码前将检测框叠加回视频帧。
- `camera_gateway/src/TcpServer.cpp`
  负责摄像头上送流的重组和 `HTTP/H.264` 分发。

### AI 与可观测性

- `ai/src/RknnDetector.cpp`
  封装 `rknn_init / rknn_run / rknn_outputs_get`，并提供通用检测结果解析。
- `event/src/EventLogger.cpp`
  把检测结果写成控制台摘要和 `JSONL` 事件流。
- `metrics/src/MetricsCollector.cpp`
  从 `/proc` 采集进程 CPU、RSS，并统计编码/推理 FPS。

## RKNN 接入说明

当前 `RknnDetector` 支持的输出布局以“已解码检测结果”或“行优先检测 head”为主，适合这些常见输出：

- `[N, 6]`
- `[1, N, 6]`
- `[1, N, 85]`
- `[1, 84, N]`

推荐优先选用轻量目标检测模型，并尽量选择便于端侧后处理的输出形式。若模型输出是多尺度 raw head，可继续按模型特性扩展 `RknnDetector.cpp` 的解析逻辑。

## 交叉编译

### 方式 1：顶层一次性构建

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

### 方式 2：按模块构建

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

## 运行方式

### 1. 环境变量

参考 `config/aicam.env.example`，至少设置：

- `AICAM_GATEWAY_IP`
- `AICAM_GATEWAY_PORT`
- `AICAM_RKNN_MODEL`
- `AICAM_LABELS`
- `AICAM_OVERLAY_ENABLE`

如果不设置 `AICAM_RKNN_MODEL`，程序会自动退化为纯视频链路与指标统计模式。

### 2. 启动

```bash
cd <repo-root>
bash scripts/start_aicam.sh
```

如需整理板端运行目录：

```bash
bash scripts/package_runtime.sh
```

### 3. 主机侧播放

推荐低延迟播放方式：

```bash
adb forward tcp:18080 tcp:8080
ffplay -fflags nobuffer -flags low_delay -framedrop -sync video -probesize 32 -analyzeduration 0 http://127.0.0.1:18080/
```

也可使用 VLC 打开：

```text
http://<board-ip>:8080/
```

当 `AICAM_OVERLAY_ENABLE=1` 且 AI 检测到目标时，编码前会在视频帧上叠加白色矩形框。

## 运行输出

- 事件日志：`logs/events.jsonl`
- 指标日志：`logs/metrics.jsonl`
- 网关输出：`logs/gateway.out`

## 文档

- [部署说明](docs/DEPLOYMENT.md)

## 项目亮点

- 板端视频采集、编码、推理、事件输出并行运行
- 模型缺失或运行库缺失时可自动降级，保证视频链路可用
- 推理结果可同时输出到 `JSONL` 与视频画面，便于演示与验收
- 运行时可持续记录 `FPS / CPU / RSS / Throughput`，方便做性能对比

## 已知限制

- 当前网关输出仍为 `HTTP + 裸 H.264`
- 摄像头设备默认仍为 `/dev/video13`
- 默认输入分辨率仍为 `1920x1080`
- RKNN 后处理目前优先面向 YOLO 风格检测输出，不覆盖所有模型
- 当前尚未实现截图留存和 Web 展示层
