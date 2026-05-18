#pragma once

#include <cstdint>
#include <vector>

// 线程之间传递视频帧的结构体
struct CapturedFrame {
    // 帧序号
    uint64_t sequence = 0;
    // 采集时间戳
    uint64_t capture_time_us = 0;

    int width = 0;
    int height = 0;
    // 原始图像数据
    std::vector<uint8_t> nv12;
};
