#pragma once

#include <string>

struct AiConfig {
    bool enabled = false;
    std::string model_path;
    std::string labels_path;
    float score_threshold = 0.35f;
    float nms_threshold = 0.45f;
    int infer_every_n_frames = 5;
    int max_results = 20;
    bool has_objectness = true;
    std::string box_format = "xywh";
};
