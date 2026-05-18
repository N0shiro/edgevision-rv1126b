#include "DetectionOverlay.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr uint8_t kOverlayOutlineLuma = 24;
constexpr uint8_t kOverlayBoxLuma = 235;

// 限制坐标在有效范围
int clampCoordinate(int value, int lower, int upper) {
    if (upper < lower) {
        return lower;
    }
    return std::max(lower, std::min(value, upper));
}

}  // namespace

DetectionOverlay::DetectionOverlay(bool enabled, int box_thickness, int stale_frame_window)
    : enabled_(enabled),
      box_thickness_(std::max(1, box_thickness)),
      stale_frame_window_(std::max(1, stale_frame_window)) {}

void DetectionOverlay::update(const ai::AiResult& result) {
    if (!enabled_ || !result.valid) {
        return;
    }
    // 加锁防止snapshot_被update和apply同时访问时出现数据竞争
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.valid = true;
    snapshot_.frame_sequence = result.frame_sequence;
    // 保存检测框列表
    snapshot_.detections = result.detections;
}

void DetectionOverlay::apply(CapturedFrame& frame) const {
    if (!enabled_ || frame.nv12.empty() || frame.width <= 0 || frame.height <= 0) {
        return;
    }

    // 使用快照机制，避免在apply里直接访问snapshot_
    // 导致update和apply之间的数据竞争
    // 锁只保护读取瞬间，后续画框就不用占用
    Snapshot snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot = snapshot_;
    }

    if (!snapshot.valid) {
        return;
    }

    if (frame.sequence < snapshot.frame_sequence) {
        return;
    }

    // 如果帧序号相差过大，说明快照已经过时了，
    // 就不画了，等update更新新快照
    if (frame.sequence - snapshot.frame_sequence > static_cast<uint64_t>(stale_frame_window_)) {
        return;
    }

    drawDetections(frame.nv12, frame.width, frame.height, snapshot.detections);
}

// 遍历检测框列表，在NV12的Y平面上画框
void DetectionOverlay::drawDetections(
    std::vector<uint8_t>& nv12,
    int width,
    int height,
    const std::vector<ai::Detection>& detections
) const {
    if (detections.empty()) {
        return;
    }

    const size_t y_plane_size = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (nv12.size() < y_plane_size) {
        return;
    }

    uint8_t* y_plane = nv12.data();
    const int max_x = width - 1;
    const int max_y = height - 1;
    const int thickness = std::max(1, std::min(box_thickness_, std::min(width, height)));
    const int outline_thickness = std::min(thickness + 1, std::min(width, height));

    for (const auto& detection : detections) {
        const int x1 = clampCoordinate(static_cast<int>(std::lround(detection.x1)), 0, max_x);
        const int y1 = clampCoordinate(static_cast<int>(std::lround(detection.y1)), 0, max_y);
        const int x2 = clampCoordinate(static_cast<int>(std::lround(detection.x2)), 0, max_x);
        const int y2 = clampCoordinate(static_cast<int>(std::lround(detection.y2)), 0, max_y);

        if (x2 <= x1 || y2 <= y1) {
            continue;
        }

        drawRectangleY(
            y_plane,
            width,
            height,
            x1 - 1,
            y1 - 1,
            x2 + 1,
            y2 + 1,
            outline_thickness,
            kOverlayOutlineLuma
        );
        drawRectangleY(
            y_plane,
            width,
            height,
            x1,
            y1,
            x2,
            y2,
            thickness,
            kOverlayBoxLuma
        );
    }
}

// 修改y平面
void DetectionOverlay::drawRectangleY(
    uint8_t* y_plane,
    int width,
    int height,
    int x1,
    int y1,
    int x2,
    int y2,
    int thickness,
    uint8_t luma_value
) const {
    if (y_plane == nullptr || width <= 0 || height <= 0 || thickness <= 0) {
        return;
    }

    // 整理矩形边界
    const int left = clampCoordinate(std::min(x1, x2), 0, width - 1);
    const int right = clampCoordinate(std::max(x1, x2), 0, width - 1);
    const int top = clampCoordinate(std::min(y1, y2), 0, height - 1);
    const int bottom = clampCoordinate(std::max(y1, y2), 0, height - 1);

    if (right <= left || bottom <= top) {
        return;
    }

    // 根据厚度画边框
    for (int offset = 0; offset < thickness; ++offset) {
        const int top_row = top + offset;
        const int bottom_row = bottom - offset;
        const int left_col = left + offset;
        const int right_col = right - offset;

        if (top_row > bottom_row || left_col > right_col) {
            break;
        }

        std::fill_n(
            y_plane + static_cast<size_t>(top_row) * width + left_col,
            right_col - left_col + 1,
            luma_value
        );

        if (bottom_row != top_row) {
            std::fill_n(
                y_plane + static_cast<size_t>(bottom_row) * width + left_col,
                right_col - left_col + 1,
                luma_value
            );
        }

        for (int row = top_row; row <= bottom_row; ++row) {
            y_plane[static_cast<size_t>(row) * width + left_col] = luma_value;
            y_plane[static_cast<size_t>(row) * width + right_col] = luma_value;
        }
    }
}
