#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <sys/epoll.h>
#include <vector>
#include <map>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <cstdint>

//  专属观众席位
struct Viewer {
    int fd;
    std::deque<std::vector<uint8_t>> send_queue; // 专属发送队列 (弹药库)
    size_t current_offset;                       // 当前发送进度条 (断点续传)
    std::mutex mtx;                              // 保护队列的专属锁
    bool active;                                 // 是否在线
};

//  专门记录摄像头连接的半包接收状态。
//  因为 TCP 没有“包边界”，所以必须自己把一帧一帧重新拼回来。
struct ProducerState {
    std::vector<uint8_t> recv_buffer;
};

class TcpServer {
public:
    TcpServer(int port);
    ~TcpServer();
    void start();

private:
    int port;
    int server_fd;
    int epoll_fd;
    
    //  观众花名册
    std::map<int, std::shared_ptr<Viewer>> viewers;
    std::mutex viewers_mtx; // 保护花名册的锁

    //  摄像头花名册：记录每个生产者连接还没拼完的 H.264 包
    std::map<int, ProducerState> producers;
    std::mutex producers_mtx;

    //  新连接刚接入时，先缓存首包数据，判断它到底是 VLC(GET) 还是摄像头(长度前缀流)
    std::map<int, std::vector<uint8_t>> pending_probe;

    //  专属发送线程相关
    std::thread sender_thread;
    std::condition_variable sender_cv;
    std::mutex sender_mtx;
    std::atomic<bool> is_running;

    //  缓存最近一帧带 SPS/PPS/IDR 的关键数据，新观众接入时先补这帧，更容易立刻出画
    std::vector<uint8_t> bootstrap_config;
    std::vector<uint8_t> bootstrap_frame;
    std::mutex bootstrap_mtx;

    static void setNonBlocking(int fd);
    
    // 核心引擎
    void senderLoop();                           // 专门负责发送的后台线程
    void removeViewer(int fd);                   // 安全清理观众资源的函数
    void handleViewerRequest(int fd);            //  处理 VLC 的 HTTP 接入
    bool handleProducerBytes(int fd, const uint8_t* data, size_t size); //  按长度前缀重组完整 H.264 包
    void handleCompleteFrame(const std::vector<uint8_t>& frame);        //  处理一帧完整视频包
    void queueFrameForViewers(const std::vector<uint8_t>& frame);       //  广播给所有在线观众
};

#endif
