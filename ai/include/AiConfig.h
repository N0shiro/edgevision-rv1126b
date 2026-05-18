#pragma once

#include <string>
// 应该是ai检测相关的默认配置-5.11留
struct AiConfig {
    // 是否启用 AI 推理；未配置模型时默认关闭，只保留视频链路。
    bool enabled = false;
    // RKNN 模型文件路径，由 AICAM_RKNN_MODEL 配置。
    std::string model_path;
    // 标签文件路径，由 AICAM_LABELS 配置，用于把类别 ID 转成人类可读名称。
    std::string labels_path;
    // 置信度阈值，低于该分数的检测结果会被过滤。
    float score_threshold = 0.35f;
    // NMS 阈值，用于合并高度重叠的检测框。
    float nms_threshold = 0.45f;
    // 每隔多少帧做一次 AI 推理，数值越大 CPU/NPU 压力越小但检测更新越慢。
    int infer_every_n_frames = 5;
    // 单帧最多保留的检测结果数量。
    int max_results = 20;
    // 模型输出是否包含 objectness 分支，常见 YOLO 模型需要开启。
    bool has_objectness = true;
    // 检测框格式，默认 xywh 表示中心点 x/y 加宽高。
    std::string box_format = "xywh";
};
