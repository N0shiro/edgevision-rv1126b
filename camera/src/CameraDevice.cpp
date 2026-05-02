#include "CameraDevice.h"

#include <algorithm>
#include <cstdint>

namespace {
void print_fourcc(__u32 fourcc) {
    char tag[5] = {
        static_cast<char>(fourcc & 0xFF),
        static_cast<char>((fourcc >> 8) & 0xFF),
        static_cast<char>((fourcc >> 16) & 0xFF),
        static_cast<char>((fourcc >> 24) & 0xFF),
        '\0'
    };
    std::cout << " 实际协商像素格式: " << tag << std::endl;
}

uint32_t derive_vir_height(uint32_t size_image, uint32_t bytes_per_line, uint32_t visible_height) {
    if (size_image == 0 || bytes_per_line == 0) {
        return visible_height;
    }

    uint64_t numerator = static_cast<uint64_t>(size_image) * 2;
    uint64_t denominator = static_cast<uint64_t>(bytes_per_line) * 3;
    if (denominator == 0) {
        return visible_height;
    }

    uint32_t derived = static_cast<uint32_t>(numerator / denominator);
    return std::max(derived, visible_height);
}
}

// 构造函数
CameraDevice::CameraDevice()
    : fd(-1),
      buffers(nullptr),
      bufferCount(0),
      width(0),
      height(0),
      bytesPerLine(0),
      sizeImage(0),
      sourceVirHeight(0) {
    std::cout << "CameraDevice 初始化 " << std::endl;
    
    // 1. 切换到 ISP 输出节点
    fd = open("/dev/video13", O_RDWR);
    if (fd < 0) {
        std::cerr << "摄像头打开失败。请检查是否使用 root 权限运行！" << std::endl;
        perror("error");
        exit(EXIT_FAILURE);
    }
    std::cout << "成功握手，文件描述符 fd = " << fd << std::endl;
    
    struct v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        std::cerr << "查询摄像头能力失败。" << std::endl;
        perror("error");
        close(fd);
        exit(EXIT_FAILURE);
    }
    
    // 2. 检查多平面 (MPLANE) 捕捉能力
    if ((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) == 0){
        std::cerr << "设备不支持视频捕捉或非多平面(Multiplanar)架构。" << std::endl;
        close(fd);
        exit(EXIT_FAILURE);
    }
    std::cout << "硬件摄像头驱动: " << cap.driver << std::endl;
}

CameraDevice::~CameraDevice(){
    for (int i = 0; i < bufferCount; ++i) {
        if (buffers[i].start != nullptr && buffers[i].start != MAP_FAILED) {
            munmap(buffers[i].start, buffers[i].length);
        }
    }

    if (fd >= 0) {
        close(fd);
        std::cout << "CameraDevice 已安全关闭。" << std::endl;
    }
    delete[] buffers; // 释放缓冲区数组
}

void CameraDevice::initCamera() {
    std::cout << "正在初始化多平面摄像头..." << std::endl;
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt)); 
    
    // 3. 配置为多平面格式
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; 
    fmt.fmt.pix_mp.width = 1920;  // 1080P 宽度
    fmt.fmt.pix_mp.height = 1080; // 1080P 高度
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12; // NV12 格式
    fmt.fmt.pix_mp.field = V4L2_FIELD_ANY; 

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        std::cerr << "设置摄像头格式失败。" << std::endl;
        perror("error");
        close(fd);
        exit(EXIT_FAILURE);
    }

    width = static_cast<int>(fmt.fmt.pix_mp.width);
    height = static_cast<int>(fmt.fmt.pix_mp.height);
    bytesPerLine = fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
    if (bytesPerLine == 0) {
        bytesPerLine = static_cast<uint32_t>(width);
    }
    sizeImage = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
    sourceVirHeight = derive_vir_height(sizeImage, bytesPerLine, static_cast<uint32_t>(height));

    print_fourcc(fmt.fmt.pix_mp.pixelformat);
    std::cout << " 实际协商分辨率: " << width
              << "x" << height << std::endl;
    std::cout << " 实际 bytesperline: " << bytesPerLine
              << " | sizeimage: " << sizeImage
              << " | 推导源 VirHeight: " << sourceVirHeight << std::endl;

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req)); 
    req.count = 4; 
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; // 多平面缓冲
    req.memory = V4L2_MEMORY_MMAP; 
    
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        std::cerr << "请求缓冲区失败。" << std::endl;
        perror("error");
        close(fd);
        exit(EXIT_FAILURE);
    }
    
    bufferCount = req.count; 
    buffers = new VideoBuffer[bufferCount]; 
    
    for (int i = 0 ; i < bufferCount ;++i){
        struct v4l2_buffer buf;
        struct v4l2_plane planes[1]; // 4. 必须定义平面数组
        
        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i; 
        buf.m.planes = planes; // 绑定平面指针
        buf.length = 1;        // NV12 在此驱动中占用 1 个平面

        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            std::cerr << "查询缓冲区失败。" << std::endl;
            perror("error");
            close(fd);
            exit(EXIT_FAILURE);
        }

        buffers[i].length = buf.m.planes[0].length; 
        buffers[i].start = mmap(NULL, buf.m.planes[0].length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.planes[0].m.mem_offset); 
        
        if (buffers[i].start == MAP_FAILED) {
            std::cerr << "内存映射失败。" << std::endl;
            perror("error");
            close(fd);
            exit(EXIT_FAILURE);
        }
        
        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            std::cerr << "缓冲区入队 (QBUF) 失败。" << std::endl;
            perror("error");
            close(fd);
            exit(EXIT_FAILURE);
        }
    }
    std::cout << " 4个多平面缓冲区内存映射完毕，并已入队！" << std::endl;
}

void CameraDevice::startStream() {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        std::cerr << "开启视频流失败！" << std::endl;
        exit(EXIT_FAILURE);
    }
    std::cout << " ISP 硬件加速视频流已启动！" << std::endl;
}

bool CameraDevice::captureFrame(unsigned char* out_yuv) {
    struct v4l2_buffer buf;
    struct v4l2_plane planes[1];
    
    memset(&buf, 0, sizeof(buf));
    memset(planes, 0, sizeof(planes));
    
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.m.planes = planes;
    buf.length = 1;

    fd_set fds; FD_ZERO(&fds); FD_SET(fd, &fds);
    struct timeval tv; tv.tv_sec = 2; tv.tv_usec = 0;
    int r = select(fd + 1, &fds, NULL, NULL, &tv);
    if (r <= 0) {
        std::cerr << " 抓图超时或出错！" << std::endl;
        return false;
    }

    if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) return false;

    const auto* src = static_cast<const uint8_t*>(buffers[buf.index].start);
    uint32_t frameBytes = buf.m.planes[0].bytesused;
    uint32_t sourceStride = bytesPerLine == 0 ? static_cast<uint32_t>(width) : bytesPerLine;
    uint32_t runtimeVirHeight = derive_vir_height(
        frameBytes == 0 ? sizeImage : frameBytes,
        sourceStride,
        static_cast<uint32_t>(height)
    );
    const size_t packedYSize = static_cast<size_t>(width) * height;

    const uint8_t* srcY = src;
    const uint8_t* srcUV = src + static_cast<size_t>(sourceStride) * runtimeVirHeight;
    uint8_t* dstY = out_yuv;
    uint8_t* dstUV = out_yuv + packedYSize;

    for (int row = 0; row < height; ++row) {
        std::memcpy(
            dstY + static_cast<size_t>(row) * width,
            srcY + static_cast<size_t>(row) * sourceStride,
            static_cast<size_t>(width)
        );
    }

    for (int row = 0; row < height / 2; ++row) {
        std::memcpy(
            dstUV + static_cast<size_t>(row) * width,
            srcUV + static_cast<size_t>(row) * sourceStride,
            static_cast<size_t>(width)
        );
    }

    if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) return false;

    return true;
}

int CameraDevice::getWidth() const {
    return width;
}

int CameraDevice::getHeight() const {
    return height;
}

size_t CameraDevice::getPackedFrameSize() const {
    return static_cast<size_t>(width) * height * 3 / 2;
}
