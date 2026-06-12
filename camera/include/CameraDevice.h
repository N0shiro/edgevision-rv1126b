#pragma once

#include <cstddef>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <cerrno>     // 包含错误码
#include <cstdio>     // 包含 perror
#include <sys/ioctl.h>          // 核心：提供 ioctl 系统调用
#include <linux/videodev2.h>    // 核心：包含 V4L2 的所有宏定义和结构体 (如 V4L2_PIX_FMT_YUYV)
#include <cstring>              // 提供 memset (清空内存用)
#include <cstdlib>              // 提供 exit()
#include <sys/mman.h>          // 提供 mmap 和 munmap 用于内存映射
#include <string>

struct VideoBuffer {
    void* start;
    size_t length;
};

class CameraDevice {    
public:
    CameraDevice(std::string device_path, int requested_width, int requested_height);
    ~CameraDevice();
    void initCamera();
    void startStream();
    void captureOneTestFrame();
    bool captureFrame(unsigned char* out_yuv);
    int getWidth() const;
    int getHeight() const;
    size_t getPackedFrameSize() const;
private:
    int fd; // 文件描述符，代表与摄像头设备的连接   
    std::string device_path;
    int requested_width;
    int requested_height;
    VideoBuffer* buffers; // 用于存储内存映射的缓冲区信息
    int bufferCount; // 缓冲区的数量
    int width;
    int height;
    uint32_t bytesPerLine;
    uint32_t sizeImage;
    uint32_t sourceVirHeight;
};
