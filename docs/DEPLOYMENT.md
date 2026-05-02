# edgevision-rv1126b 部署说明

## 1. 目标

把 `edgevision-rv1126b` 部署到 RV1126B 板端，并跑通下面这条链路：

`camera -> gateway -> VLC`

同时启用：

- RKNN 模型推理
- 事件 JSONL 输出
- 性能指标落盘

## 2. PC 端准备

### 交叉编译

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

### 转换 RKNN 模型

```bash
python3 scripts/convert_to_rknn.py \
  --onnx /path/to/yolov5n.onnx \
  --output models/yolov5n.rknn \
  --target rv1126b \
  --quantize \
  --dataset /path/to/dataset.txt
```

### 打包运行时目录

```bash
bash scripts/package_runtime.sh
```

默认会额外打包：

- `librknnrt.so`
- `models/yolov5n_fp.rknn`（如果存在）
- `scripts/start_aicam_board.sh`，并在运行包中命名为 `start_aicam.sh`

如需自动打包 `librknnrt.so`，可在执行前设置：

```bash
export SDK_ROOT=<sdk-root>
```

## 3. 板端目录建议

```text
/userdata/aicam/
├─ camera
├─ camera_gateway
├─ start_aicam.sh
├─ librknnrt.so
├─ models/
│  ├─ yolov5n_fp.rknn
│  └─ coco_80_labels.txt
├─ config/
│  └─ aicam.env.example
└─ logs/
```

## 4. 板端环境变量

参考 `config/aicam.env.example`，至少设置：

```bash
export AICAM_GATEWAY_IP=127.0.0.1
export AICAM_GATEWAY_PORT=8080
export AICAM_RKNN_MODEL=/userdata/aicam/models/yolov5n_fp.rknn
export AICAM_LABELS=/userdata/aicam/models/coco_80_labels.txt
export AICAM_AI_ENABLE=1
export AICAM_INFER_EVERY_N=10
export AICAM_EVENT_JSONL=/userdata/aicam/logs/events.jsonl
export AICAM_METRICS_JSONL=/userdata/aicam/logs/metrics.jsonl
```

## 5. 启动顺序

### 方式 1：手工启动

```bash
./camera_gateway
./camera
```

### 方式 2：使用脚本

```bash
cd /userdata/aicam
bash ./start_aicam.sh
```

## 6. 验证项

### 视频链路

- 网关启动后监听 `8080`
- 采集端成功连接网关
- VLC 可打开 `http://<board-ip>:8080/`

### AI 链路

- 控制台出现 `RKNN detector ready`
- `logs/events.jsonl` 持续写入检测事件

### 指标链路

- `logs/metrics.jsonl` 持续写入指标
- 能观察到 `capture_fps / encode_fps / ai_fps / cpu_percent / rss_mb`

## 7. 常见问题

### 模型未启用

- 检查 `AICAM_RKNN_MODEL` 是否设置
- 检查 `librknnrt.so` 是否在板端可加载

### AI 有输出但框不对

- 检查 `AICAM_BOX_FORMAT`
- 检查 `AICAM_HAS_OBJECTNESS`
- 检查模型输出是否属于 `README` 中列出的支持布局

### 视频流正常但 AI 太慢

- 提高 `AICAM_INFER_EVERY_N`
- 降低模型输入尺寸
- 切换到更轻量的检测模型
