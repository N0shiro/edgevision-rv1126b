#pragma once // 防止头文件被重复包含

#include "AiConfig.h"

#include <string>

struct AppConfig {
    // 摄像头传感器编号，用于 AIQ/ISP 初始化，默认使用 0 号 sensor。
    int sensor_id = 0;
    // 摄像头采集和 H.264 编码帧率，由 AICAM_FPS 配置。
    int fps = 30;
    // IQ 文件目录路径，由 AICAM_IQ_DIR 配置，默认使用 /etc/iqfiles。
    std::string iq_dir = "/etc/iqfiles";
    // V4L2 视频设备节点，由 AICAM_VIDEO_DEVICE 配置。
    std::string video_device = "/dev/video13";
    // 摄像头采集宽度，由 AICAM_WIDTH 配置。
    int video_width = 1920;
    // 摄像头采集高度，由 AICAM_HEIGHT 配置。
    int video_height = 1080;
    // H.264 编码码率，单位 kbps，由 AICAM_BITRATE_KBPS 配置。
    int bitrate_kbps = 4096;
    // H.264 GOP 长度，由 AICAM_GOP 配置；默认跟随帧率。
    int gop = 30;

    // camera_gateway 的 TCP 地址，camera 会把 H.264 裸流推到这里。
    std::string gateway_ip = "127.0.0.1";
    // camera_gateway 的 TCP 端口。
    int gateway_port = 8080;

    // 编码队列容量，采集线程把待编码帧放入该队列。
    int encode_queue_capacity = 5;
    // AI 队列容量，采集线程把待推理帧放入该队列。
    int ai_queue_capacity = 3;
    // 是否打印每帧编码日志，开启后会输出 frame、bytes、encode_ms。
    bool verbose_encode_log = false;

    // AI 检测事件日志路径，默认写入 logs/events.jsonl。
    std::string event_jsonl_path = "logs/events.jsonl";
    // 运行指标日志路径，默认写入 logs/metrics.jsonl。
    std::string metrics_jsonl_path = "logs/metrics.jsonl";
    // 指标采样写入间隔，单位秒。
    int metrics_interval_sec = 5;

    // 是否在编码前把 AI 检测框叠加到视频帧上。
    bool overlay_enable = true;
    // 检测框线条粗细，单位像素。
    int overlay_box_thickness = 4;
    // 检测框保留帧数，避免 AI 抽帧推理时画面上的框闪烁太快。
    int overlay_stale_frames = 30;

    // AI/RKNN 推理相关配置。
    AiConfig ai;

    // 从 AICAM_* 环境变量读取配置，并对非法值做兜底修正。
    static AppConfig fromEnvironment();
    // 启动时打印最终配置，方便确认实际运行参数。
    void printSummary() const;
};
