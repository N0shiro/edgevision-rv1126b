#pragma once
#include <cstddef>
#include <stdint.h>
#include <vector>
#include <array>

extern "C" {
#include <rk_mpi_cal.h>
#include <rk_mpi_sys.h>
#include <rk_mpi_mb.h>
#include <rk_mpi_venc.h>
}

class H264Encoder {
public:
    H264Encoder(int width, int height, int fps, int bitrate_kbps, int gop);
    ~H264Encoder();

    bool encode(unsigned char* yuv_data, std::vector<uint8_t>& out_h264);

private:
    bool initEncoder();
    void destroyEncoder();

    int width;
    int height;
    int fps;
    int bitrate_kbps;
    int gop;
    int channel_id;
    bool sys_initialized;
    bool channel_created;
    uint64_t pts;
    size_t frame_size;
    size_t input_buffer_size;
    uint32_t vir_width;
    uint32_t vir_height;
    PIXEL_FORMAT_E input_pixel_format;
    MB_BLK input_buffer;
    uint8_t* input_vir_addr;
    std::array<VENC_PACK_S, 8> packs;
    VENC_STREAM_S stream;
};
