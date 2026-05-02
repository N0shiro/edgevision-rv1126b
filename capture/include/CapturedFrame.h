#pragma once

#include <cstdint>
#include <vector>

struct CapturedFrame {
    uint64_t sequence = 0;
    uint64_t capture_time_us = 0;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> nv12;
};
