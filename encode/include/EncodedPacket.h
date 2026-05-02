#pragma once

#include <cstdint>
#include <vector>

struct EncodedPacket {
    uint64_t source_frame_sequence = 0;
    uint64_t encode_time_us = 0;
    std::vector<uint8_t> payload;
};
