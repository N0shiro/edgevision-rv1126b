#pragma once

#include "AiTypes.h"
#include "CapturedFrame.h"

#include <cstdint>
#include <mutex>
#include <vector>

class DetectionOverlay {
public:
    DetectionOverlay(bool enabled, int box_thickness, int stale_frame_window);

    void update(const ai::AiResult& result);
    void apply(CapturedFrame& frame) const;

private:
    struct Snapshot {
        bool valid = false;
        uint64_t frame_sequence = 0;
        std::vector<ai::Detection> detections;
    };

    void drawDetections(
        std::vector<uint8_t>& nv12,
        int width,
        int height,
        const std::vector<ai::Detection>& detections
    ) const;
    void drawRectangleY(
        uint8_t* y_plane,
        int width,
        int height,
        int x1,
        int y1,
        int x2,
        int y2,
        int thickness,
        uint8_t luma_value
    ) const;

    bool enabled_;
    int box_thickness_;
    int stale_frame_window_;
    mutable std::mutex mutex_;
    Snapshot snapshot_;
};
