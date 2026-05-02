#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ai {

struct Detection {
    int class_id = -1;
    std::string label;
    float score = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
};

struct AiResult {
    bool valid = false;
    uint64_t frame_sequence = 0;
    uint64_t capture_time_us = 0;
    std::string backend = "disabled";
    std::string note;
    double preprocess_ms = 0.0;
    double inference_ms = 0.0;
    double postprocess_ms = 0.0;
    std::vector<Detection> detections;
};

}  // namespace ai
