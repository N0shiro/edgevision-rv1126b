#include "H264Encoder.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {
constexpr int kStreamPackCount = 8;
constexpr int kDefaultBitrateKbps = 4096;
constexpr int kVencTimeoutMs = 2000;
constexpr RK_U32 kVencStreamBufCnt = 1;
constexpr RK_U32 kVencMaxStrmCnt = 1;
constexpr RK_U32 kVencPollWakeUpFrmCnt = 1;

PIXEL_FORMAT_E detect_input_pixel_format() {
    const char* order = std::getenv("CAMV3_CHROMA_ORDER");
    if (order != nullptr) {
        std::string value(order);
        for (char& ch : value) {
            if (ch >= 'A' && ch <= 'Z') {
                ch = static_cast<char>(ch - 'A' + 'a');
            }
        }

        if (value == "nv21" || value == "vu") {
            return RK_FMT_YUV420SP_VU;
        }
    }

    return RK_FMT_YUV420SP;
}

const char* pixel_format_name(PIXEL_FORMAT_E format) {
    return format == RK_FMT_YUV420SP_VU ? "NV21" : "NV12";
}

void copy_compact_nv12_to_aligned_buffer(
    const uint8_t* src,
    uint8_t* dst,
    int width,
    int height,
    uint32_t vir_width,
    uint32_t vir_height
) {
    const size_t compact_y_size = static_cast<size_t>(width) * height;
    const uint8_t* src_y = src;
    const uint8_t* src_uv = src + compact_y_size;
    uint8_t* dst_y = dst;
    uint8_t* dst_uv = dst + static_cast<size_t>(vir_width) * vir_height;

    for (int row = 0; row < height; ++row) {
        std::memcpy(
            dst_y + static_cast<size_t>(row) * vir_width,
            src_y + static_cast<size_t>(row) * width,
            static_cast<size_t>(width)
        );
    }

    for (int row = 0; row < height / 2; ++row) {
        std::memcpy(
            dst_uv + static_cast<size_t>(row) * vir_width,
            src_uv + static_cast<size_t>(row) * width,
            static_cast<size_t>(width)
        );
    }
}
}

H264Encoder::H264Encoder(int w, int h, int frame_rate)
    : width(w),
      height(h),
      fps(frame_rate),
      channel_id(0),
      sys_initialized(false),
      channel_created(false),
      pts(0),
      frame_size(static_cast<size_t>(w) * h * 3 / 2),
      input_buffer_size(0),
      vir_width(0),
      vir_height(0),
      input_pixel_format(detect_input_pixel_format()),
      input_buffer(RK_NULL),
      input_vir_addr(nullptr) {
    std::memset(&stream, 0, sizeof(stream));
    stream.pstPack = packs.data();
    stream.u32PackCount = kStreamPackCount;

    if (!initEncoder()) {
        std::cerr << "硬件 H.264 编码器初始化失败" << std::endl;
        std::exit(EXIT_FAILURE);
    }

    std::cout << "  编码器组装完毕 (Rockchip VENC / "
              << pixel_format_name(input_pixel_format) << ")" << std::endl;
}

H264Encoder::~H264Encoder() {
    destroyEncoder();
    std::cout << " 编码器已结束" << std::endl;
}

bool H264Encoder::encode(unsigned char* yuv_data, std::vector<uint8_t>& out_h264) {
    // 清空上一帧
    out_h264.clear();

    if (input_buffer == RK_NULL || input_vir_addr == nullptr) {
        std::cerr << " 编码输入缓冲未就绪" << std::endl;
        return false;
    }

    std::memset(input_vir_addr, 0, input_buffer_size);
    // 把输入的紧凑nv12拷贝到对齐后的编码输入buffer里，
    copy_compact_nv12_to_aligned_buffer(
        yuv_data,
        input_vir_addr,
        width,
        height,
        vir_width,
        vir_height
    );

    // 刷新缓存
    RK_S32 ret = RK_MPI_SYS_MmzFlushCache(input_buffer, RK_FALSE);
    if (ret != RK_SUCCESS) {
        std::cerr << " 刷新编码输入缓存失败: 0x" << std::hex << ret << std::dec << std::endl;
        return false;
    }

    // 构造一帧的描述信息
    VIDEO_FRAME_INFO_S frame;
    std::memset(&frame, 0, sizeof(frame));
    // 告诉编码器这一帧图像数据在 input_buffer 里，
    // 格式是 input_pixel_format，
    // 尺寸是 vir_width x vir_height
    frame.stVFrame.pMbBlk = input_buffer;
    frame.stVFrame.u32Width = static_cast<RK_U32>(width);
    frame.stVFrame.u32Height = static_cast<RK_U32>(height);
    frame.stVFrame.u32VirWidth = vir_width;
    frame.stVFrame.u32VirHeight = vir_height;
    frame.stVFrame.enPixelFormat = input_pixel_format;
    // 线性内存布局
    frame.stVFrame.enVideoFormat = VIDEO_FORMAT_LINEAR;
    frame.stVFrame.enCompressMode = COMPRESS_MODE_NONE;
    frame.stVFrame.enDynamicRange = DYNAMIC_RANGE_SDR8;
    frame.stVFrame.enColorGamut = COLOR_GAMUT_BT709;
    frame.stVFrame.enQuantRange = QUANT_RANGE_LIMIT_RANGE;
    // 给当前帧设置时间戳
    frame.stVFrame.u64PTS = pts++;

    // 把这一帧图像送给编码器
    ret = RK_MPI_VENC_SendFrame(channel_id, &frame, kVencTimeoutMs);
    if (ret != RK_SUCCESS) {
        std::cerr << " 发送硬件编码帧失败: 0x" << std::hex << ret << std::dec << std::endl;
        return false;
    }

    // 准备接受编码器输出的码流数据
    std::memset(packs.data(), 0, sizeof(VENC_PACK_S) * packs.size());
    std::memset(&stream, 0, sizeof(stream));
    stream.pstPack = packs.data();
    stream.u32PackCount = kStreamPackCount;

    // 从编码器取出码流
    ret = RK_MPI_VENC_GetStream(channel_id, &stream, kVencTimeoutMs);
    if (ret != RK_SUCCESS) {
        std::cerr << " 获取硬件编码输出失败(超时或错误): 0x" << std::hex << ret << std::dec << std::endl;
        return false;
    }

    // 把编码器输出拷贝到 out_h264 里
    for (RK_U32 i = 0; i < stream.u32PackCount; ++i) {
        // 把硬件buffer转换成cpu可访问的虚拟地址，
        void* packet_addr = RK_MPI_MB_Handle2VirAddr(stream.pstPack[i].pMbBlk);
        if (packet_addr == nullptr) {
            continue;
        }

        // 获取数据真实开始位置
        auto* packet_data = static_cast<uint8_t*>(packet_addr) + stream.pstPack[i].u32Offset;
        // 将数据追加到out_h264末尾
        out_h264.insert(
            out_h264.end(),
            packet_data,
            packet_data + stream.pstPack[i].u32Len
        );
    }

    // 释放
    RK_MPI_VENC_ReleaseStream(channel_id, &stream);
    return !out_h264.empty();
}

bool H264Encoder::initEncoder() {
    // 初始化 RK_MPI_SYS 系统，
    // 必须在使用任何 RK_MPI 功能前调用
    RK_S32 ret = RK_MPI_SYS_Init();
    if (ret != RK_SUCCESS) {
        std::cerr << " RK_MPI_SYS_Init 失败: 0x" << std::hex << ret << std::dec << std::endl;
        return false;
    }
    sys_initialized = true;

    // 创建图片buffer属性结构体，
    // 查询编码输入所需的对齐尺寸和缓冲大小
    PIC_BUF_ATTR_S buffer_attr;
    std::memset(&buffer_attr, 0, sizeof(buffer_attr));
    buffer_attr.u32Width = static_cast<RK_U32>(width);
    buffer_attr.u32Height = static_cast<RK_U32>(height);
    buffer_attr.enPixelFormat = input_pixel_format;
    buffer_attr.enCompMode = COMPRESS_MODE_NONE;

    // 创建结构体来接收计算结果
    MB_PIC_CAL_S pic_cal;
    std::memset(&pic_cal, 0, sizeof(pic_cal));
    // 计算实际需要的缓冲大小和对齐后的尺寸
    ret = RK_MPI_CAL_COMM_GetPicBufferSize(&buffer_attr, &pic_cal);
    if (ret != RK_SUCCESS) {
        std::cerr << " RK_MPI_CAL_COMM_GetPicBufferSize 失败: 0x"
                  << std::hex << ret << std::dec << std::endl;
        return false;
    }

    vir_width = pic_cal.u32VirWidth;
    vir_height = pic_cal.u32VirHeight;
    input_buffer_size = pic_cal.u32MBSize;

    // 配置venc通道属性结构体，
    // 包含编码类型、输入格式、分辨率、码率控制等参数
    VENC_CHN_ATTR_S attr;
    std::memset(&attr, 0, sizeof(attr));
    // 编码类型：H.264 (AVC)，
    attr.stVencAttr.enType = RK_VIDEO_ID_AVC;
    // 设置profile为Baseline，兼容性更好，尤其适合低延迟场景
    attr.stVencAttr.u32Profile = H264E_PROFILE_BASELINE;
    // 设置输入格式（默认nv12）
    attr.stVencAttr.enPixelFormat = input_pixel_format;
    // 最大宽高和实际宽高
    attr.stVencAttr.u32MaxPicWidth = static_cast<RK_U32>(width);
    attr.stVencAttr.u32MaxPicHeight = static_cast<RK_U32>(height);
    attr.stVencAttr.u32PicWidth = static_cast<RK_U32>(width);
    attr.stVencAttr.u32PicHeight = static_cast<RK_U32>(height);
    // 虚拟宽高，也就是对齐后尺寸
    attr.stVencAttr.u32VirWidth = vir_width;
    attr.stVencAttr.u32VirHeight = vir_height;
    // 编码输出码流buffer数量
    attr.stVencAttr.u32StreamBufCnt = kVencStreamBufCnt;
    // buffer大小
    attr.stVencAttr.u32BufSize = static_cast<RK_U32>(input_buffer_size);
    // 按帧输出
    attr.stVencAttr.bByFrame = RK_TRUE;
    // 设置恒定码率
    attr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
    // gop长度，两个关键帧的间隔
    attr.stRcAttr.stH264Cbr.u32Gop = static_cast<RK_U32>(fps);
    attr.stRcAttr.stH264Cbr.u32BitRate = kDefaultBitrateKbps;
    // 输出和输入帧率
    attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = 1;
    attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = static_cast<RK_U32>(fps);
    attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen = 1;
    attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum = static_cast<RK_U32>(fps);

    // 正式创建编码通道
    ret = RK_MPI_VENC_CreateChn(channel_id, &attr);
    if (ret != RK_SUCCESS) {
        std::cerr << " RK_MPI_VENC_CreateChn 失败: 0x" << std::hex << ret << std::dec << std::endl;
        return false;
    }
    channel_created = true;

    // 读取当前通道参数
    VENC_CHN_PARAM_S chn_param;
    std::memset(&chn_param, 0, sizeof(chn_param));
    ret = RK_MPI_VENC_GetChnParam(channel_id, &chn_param);
    if (ret != RK_SUCCESS) {
        std::cerr << " RK_MPI_VENC_GetChnParam 失败: 0x" << std::hex << ret << std::dec << std::endl;
        return false;
    }

    // 修改成偏低延迟配置参数，并设置
    chn_param.u32MaxStrmCnt = kVencMaxStrmCnt;
    chn_param.u32PollWakeUpFrmCnt = kVencPollWakeUpFrmCnt;
    ret = RK_MPI_VENC_SetChnParam(channel_id, &chn_param);
    if (ret != RK_SUCCESS) {
        std::cerr << " RK_MPI_VENC_SetChnParam 失败: 0x" << std::hex << ret << std::dec << std::endl;
        return false;
    }

    // 启动编码器并接收帧
    VENC_RECV_PIC_PARAM_S recv_param;
    std::memset(&recv_param, 0, sizeof(recv_param));
    // 设置为-1表示一直接收，
    // 直到调用 RK_MPI_VENC_StopRecvFrame 停止
    recv_param.s32RecvPicNum = -1;
    ret = RK_MPI_VENC_StartRecvFrame(channel_id, &recv_param);
    if (ret != RK_SUCCESS) {
        std::cerr << " RK_MPI_VENC_StartRecvFrame 失败: 0x" << std::hex << ret << std::dec << std::endl;
        return false;
    }

    // 申请输入buffer
    ret = RK_MPI_SYS_MmzAlloc_Cached(
        &input_buffer,
        RK_NULL,
        RK_NULL,
        static_cast<RK_U32>(input_buffer_size)
    );
    // 申请一块 Rockchip MMZ 内存。
    // MMZ 可以理解为硬件媒体模块可以访问的内存。
    // 编码器不能随便读普通 new / malloc 出来的内存，
    // 它需要 SDK 管理的 media buffer。
    // 这里申请的是 cached buffer。
    // 所以 CPU 写入后，后面必须 flush cache
    if (ret != RK_SUCCESS) {
        std::cerr << " RK_MPI_SYS_MmzAlloc_Cached 失败: 0x" << std::hex << ret << std::dec << std::endl;
        return false;
    }

    // 把硬件buffer转换成cpu可访问的虚拟地址，
    input_vir_addr = static_cast<uint8_t*>(RK_MPI_MB_Handle2VirAddr(input_buffer));
    if (input_vir_addr == nullptr) {
        std::cerr << " 获取编码输入虚拟地址失败" << std::endl;
        return false;
    }

    std::cout << "  硬编码输入色彩顺序: " << pixel_format_name(input_pixel_format) << std::endl;
    std::cout << "  VENC 输入虚拟尺寸: " << vir_width
              << "x" << vir_height
              << " | 输入缓冲: " << input_buffer_size << " 字节" << std::endl;
    std::cout << "  VENC 低延迟通道参数: stream_buf_cnt=" << kVencStreamBufCnt
              << " | max_strm_cnt=" << kVencMaxStrmCnt
              << " | poll_wakeup_frm_cnt=" << kVencPollWakeUpFrmCnt << std::endl;
    // 请求编码器下一帧输出 IDR 帧，也就是关键帧
    RK_MPI_VENC_RequestIDR(channel_id, RK_TRUE);
    return true;
}

void H264Encoder::destroyEncoder() {
    if (channel_created) {
        RK_MPI_VENC_StopRecvFrame(channel_id);
        RK_MPI_VENC_DestroyChn(channel_id);
        channel_created = false;
    }

    if (input_buffer != RK_NULL) {
        RK_MPI_SYS_MmzFree(input_buffer);
        input_buffer = RK_NULL;
        input_vir_addr = nullptr;
    }

    if (sys_initialized) {
        RK_MPI_SYS_WaitFreeMB();
        RK_MPI_SYS_Exit();
        sys_initialized = false;
    }
}
