#include "TcpServer.h"
#include <iostream>
#include <cstring>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstdlib>
#include <csignal>
#include <chrono>
#include <sys/socket.h>
#include <arpa/inet.h>

namespace {
// 设置单个帧最大尺寸，防止异常长度把网关内存直接打爆。
constexpr uint32_t MAX_FRAME_SIZE = 2 * 1024 * 1024;
// 低延迟模式下，把单个客户端允许堆积的完整帧数压得更小。
// 30fps 下 15 帧大约是 0.5 秒，可明显抑制“越看越慢”的历史帧积压。
constexpr size_t MAX_QUEUED_FRAMES = 15;
// “协议嗅探”首包探测至少要读到 4 字节，才能判断是不是 "GET "。
constexpr size_t PROBE_BYTES = 4;

// 查找 Annex B 起始码，用于识别 SPS/PPS/IDR。
size_t findStartCode(const std::vector<uint8_t>& data, size_t from, size_t& prefix_len) {
    for (size_t i = from; i + 3 < data.size(); ++i) {
        if (data[i] == 0 && data[i + 1] == 0) { //
            if (data[i + 2] == 1) {
                prefix_len = 3;
                return i;
            }
            if (i + 3 < data.size() && data[i + 2] == 0 && data[i + 3] == 1) {
                prefix_len = 4;
                return i;
            }
        }
    }
    return std::string::npos;
}

// [修改说明] 判断完整帧里是否包含某种 NALU 类型。
// [修改说明] IDR(type=5) 到来时会顺手更新 bootstrap_frame，方便新观众快速起播。
bool containsNalType(const std::vector<uint8_t>& data, uint8_t nal_type) {
    size_t offset = 0;
    while (true) {
        size_t prefix_len = 0;
        size_t start = findStartCode(data, offset, prefix_len);
        if (start == std::string::npos) {
            return false;
        }

        size_t nal_index = start + prefix_len;
        if (nal_index < data.size() && (data[nal_index] & 0x1F) == nal_type) {
            return true;
        }

        offset = nal_index + 1;
    }
}

std::vector<uint8_t> extractBootstrapConfig(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> config;
    size_t offset = 0;

    while (true) {
        size_t prefix_len = 0;
        size_t start = findStartCode(data, offset, prefix_len);
        if (start == std::string::npos) {
            break;
        }

        size_t nal_index = start + prefix_len;
        if (nal_index >= data.size()) {
            break;
        }

        size_t next_prefix_len = 0;
        size_t next_start = findStartCode(data, nal_index + 1, next_prefix_len);
        size_t nal_end = next_start == std::string::npos ? data.size() : next_start;
        uint8_t nal_type = data[nal_index] & 0x1F;

        // 为新观众保留 H.264 启动所需的关键头：SPS / PPS，顺手保留 SEI。
        if (nal_type == 7 || nal_type == 8 || nal_type == 6) {
            config.insert(config.end(), data.begin() + static_cast<std::ptrdiff_t>(start),
                          data.begin() + static_cast<std::ptrdiff_t>(nal_end));
        }

        offset = nal_index + 1;
    }

    return config;
}
}

TcpServer::TcpServer(int port) : port(port), server_fd(-1), epoll_fd(-1), is_running(true) {
    //  忽略 SIGPIPE，避免对已断开的 VLC 发送数据时整个网关被系统直接打死。
    signal(SIGPIPE, SIG_IGN);
    
    //ipv4 TCP ，
    // 接听新客户的连接请求（处理 TCP 的三次握手）
    // 不负责接收实际的视频数据或发消息
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    epoll_fd = epoll_create1(0) ;
    TcpServer::setNonBlocking(server_fd);//设置非阻塞

    if (server_fd < 0 || epoll_fd < 0) {
        std::cerr << " 网关底层初始化失败！" << std::endl;
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));//允许端口复用

    struct epoll_event event;//监控规则表
    event.events = EPOLLIN;//监听可读事件，即收到tcp请求
    event.data.fd = server_fd;//监控对象
    // 把 server_fd 作为节点挂到 epoll 红黑树上，专门监视新连接的到来
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event);

    struct sockaddr_in server_addr;//IPv4 网络空白地址登记表
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;//接收任何ip地址发来的连接请求
    server_addr.sin_port = htons(port);//小端序转大端

    //绑定地址到监控上，并开始监听，SOMAXCONN 是系统允许的最大等待连接数，通常是 128 或更大
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) exit(EXIT_FAILURE);
    if (listen(server_fd, SOMAXCONN) < 0) exit(EXIT_FAILURE);

    //  启动专门的 Sender 发送线程
    sender_thread = std::thread(&TcpServer::senderLoop, this);

    std::cout << " 异步流媒体大厅已启！监听端口: " << port << std::endl;
}

void TcpServer::setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

//  安全清理观众资源
void TcpServer::removeViewer(int fd) {
    // [修改说明] 虽然函数名沿用 removeViewer，但现在统一清理：
    // [修改说明] 1. VLC 观众连接
    // [修改说明] 2. 摄像头生产者连接
    // [修改说明] 3. 尚未判定身份的新连接
    bool should_close = false;
    bool was_viewer = false;

    {
        std::lock_guard<std::mutex> lock(viewers_mtx);
        auto it = viewers.find(fd);
        if (it != viewers.end()) {
            std::cout << " 观众 [" << fd << "] 离开大厅，清理队列资源。剩余人数: "
                      << (viewers.size() - 1) << std::endl;
            viewers.erase(it);
            should_close = true;
            was_viewer = true;
        }
    }

    {
        std::lock_guard<std::mutex> lock(producers_mtx);
        if (producers.erase(fd) > 0) {
            std::cout << " 摄像头连接 [" << fd << "] 已离线，清理缓存状态。" << std::endl;
            should_close = true;
        }
        if (pending_probe.erase(fd) > 0) {
            should_close = true;
        }
    }

    if (!should_close) {
        return;
    }

    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);

    if (!was_viewer) {
        std::cout << " 已完成连接 [" << fd << "] 的统一清理。" << std::endl;
    }
}

// [修改说明] 专门处理 VLC 的 HTTP 接入。
// [修改说明] 新观众接入后，先把 HTTP 头入队，再补最近的关键启动帧。
void TcpServer::handleViewerRequest(int fd) {
    std::cout << " HTTP 请求到达,专属发送队列:FD [" << fd << "]" << std::endl;

    auto viewer = std::make_shared<Viewer>();
    viewer->fd = fd;
    viewer->current_offset = 0;
    viewer->active = true;

    // 构造标准的 HTTP 响应头，告诉 VLC：这是一段连续的 H.264 视频流！
    std::string header =
        "HTTP/1.1 200 OK\r\n"
        "Connection: keep-alive\r\n"
        "Cache-Control: no-cache\r\n"
        "Pragma: no-cache\r\n"
        "Content-Type: video/h264\r\n"
        "\r\n";

    viewer->send_queue.push_back(std::vector<uint8_t>(header.begin(), header.end()));

    {
        std::lock_guard<std::mutex> lock(bootstrap_mtx);
        if (!bootstrap_config.empty()) {
            viewer->send_queue.push_back(bootstrap_config);
        }
        if (!bootstrap_frame.empty()) {
            // [修改说明] 把最近一帧可起播关键帧也塞给新观众，减少“只见 VLC logo 不出画”的概率。
            viewer->send_queue.push_back(bootstrap_frame);
        }
    }

    {
        std::lock_guard<std::mutex> lock(viewers_mtx);
        viewers[fd] = viewer;
    }

    sender_cv.notify_one(); // 敲锣叫醒发送线程干活！
}

// [修改说明] 这里把摄像头发来的“长度前缀 + 帧数据”流重新拼成完整一帧。
// [修改说明] 只有拼出一整帧之后，才允许进入后续广播流程。
bool TcpServer::handleProducerBytes(int fd, const uint8_t* data, size_t size) {
    std::vector<std::vector<uint8_t>> ready_frames;
    bool invalid_stream = false;

    {
        std::lock_guard<std::mutex> lock(producers_mtx);
        auto& state = producers[fd];
        state.recv_buffer.insert(state.recv_buffer.end(), data, data + size);

        while (state.recv_buffer.size() >= sizeof(uint32_t)) {
            uint32_t net_size = 0;
            memcpy(&net_size, state.recv_buffer.data(), sizeof(net_size));
            uint32_t frame_size = ntohl(net_size);

            if (frame_size == 0 || frame_size > MAX_FRAME_SIZE) {
                invalid_stream = true;
                break;
            }

            size_t total_needed = sizeof(uint32_t) + static_cast<size_t>(frame_size);
            if (state.recv_buffer.size() < total_needed) {
                break; // 半包还没收齐，继续等下一次 read
            }

            ready_frames.emplace_back(
                state.recv_buffer.begin() + sizeof(uint32_t),
                state.recv_buffer.begin() + total_needed
            );

            state.recv_buffer.erase(
                state.recv_buffer.begin(),
                state.recv_buffer.begin() + total_needed
            );
        }

        if (invalid_stream) {
            state.recv_buffer.clear();
        }
    }

    if (invalid_stream) {
        std::cerr << " 摄像头流格式异常：长度前缀非法，准备断开该连接。" << std::endl;
        return false;
    }

    for (const auto& frame : ready_frames) {
        handleCompleteFrame(frame);
    }

    return true;
}

// [修改说明] 完整帧到达后，在这里做“关键帧缓存 + 广播”两件事。
void TcpServer::handleCompleteFrame(const std::vector<uint8_t>& frame) {
    std::vector<uint8_t> config = extractBootstrapConfig(frame);
    if (!config.empty()) {
        std::lock_guard<std::mutex> lock(bootstrap_mtx);
        bootstrap_config = std::move(config);
    }

    if (containsNalType(frame, 5)) {
        std::lock_guard<std::mutex> lock(bootstrap_mtx);
        bootstrap_frame = frame;
        // [修改说明] 对硬编码链路不再假设“IDR 一定自带 SPS/PPS”。
        // [修改说明] 现在由 bootstrap_config 单独缓存配置头，再与最近 IDR 组合给新观众起播。
    }

    queueFrameForViewers(frame);
}

// [修改说明] 低延迟优化：观众过慢时，不再继续累积历史帧，也不再直接踢掉。
// [修改说明] 策略改为“保留正在发送的数据，丢弃最旧的未发送完整帧，优先追最新画面”。
void TcpServer::queueFrameForViewers(const std::vector<uint8_t>& frame) {
    std::vector<int> dead_fds;

    {
        std::lock_guard<std::mutex> lock(viewers_mtx);
        for (auto& pair : viewers) {
            auto viewer = pair.second;
            std::lock_guard<std::mutex> q_lock(viewer->mtx);

            if (!viewer->active) {
                dead_fds.push_back(viewer->fd);
                continue;
            }

            if (viewer->send_queue.size() >= MAX_QUEUED_FRAMES) {
                size_t dropped_frames = 0;

                if (viewer->current_offset == 0) {
                    while (viewer->send_queue.size() >= MAX_QUEUED_FRAMES) {
                        viewer->send_queue.pop_front();
                        ++dropped_frames;
                    }
                } else {
                    while (viewer->send_queue.size() >= MAX_QUEUED_FRAMES && viewer->send_queue.size() > 1) {
                        auto stale_it = viewer->send_queue.begin();
                        ++stale_it; // 保留当前正在发送的 chunk，只丢后面的历史完整帧。
                        viewer->send_queue.erase(stale_it);
                        ++dropped_frames;
                    }
                }

                if (dropped_frames > 0) {
                    std::cerr << " 观众 [" << viewer->fd
                              << "] 出现积压，已丢弃 " << dropped_frames
                              << " 帧旧数据，优先追最新画面。" << std::endl;
                }
            }

            viewer->send_queue.push_back(frame);
        }
    }

    if (!dead_fds.empty()) {
        for (int fd : dead_fds) {
            removeViewer(fd);
        }
    }

    sender_cv.notify_one(); // 敲锣叫醒发送线程！
}

// ==========================================
//  核心引擎 1：专门处理发送、重试、断点续传的后台线程！
// ==========================================
void TcpServer::senderLoop() {
    while (is_running) {
        // 1. 稍微等一下，如果有新数据会被立刻唤醒，防 CPU 空转
        std::unique_lock<std::mutex> lock(sender_mtx);
        sender_cv.wait_for(lock, std::chrono::milliseconds(5));
        lock.unlock();

        std::vector<int> dead_fds; // 收集已经断开的观众

        {
            std::lock_guard<std::mutex> v_lock(viewers_mtx);
            for (auto& pair : viewers) {
                auto viewer = pair.second;
                std::lock_guard<std::mutex> q_lock(viewer->mtx);

                // 2. 只要这个观众的弹药库里还有数据，就一直尝试发！
                while (!viewer->send_queue.empty() && viewer->active) {
                    auto& chunk = viewer->send_queue.front();
                    
                    // 3. 核心绝杀：断点续传！从 current_offset 的位置开始发剩下的！
                    ssize_t sent = send(
                        viewer->fd,
                        chunk.data() + viewer->current_offset,
                        chunk.size() - viewer->current_offset,
                        MSG_NOSIGNAL | MSG_DONTWAIT
                    );

                    if (sent > 0) {
                        viewer->current_offset += static_cast<size_t>(sent);
                        // 如果这一块数据全部发完了，才能从队列里扔掉！
                        if (viewer->current_offset == chunk.size()) {
                            viewer->send_queue.pop_front();
                            viewer->current_offset = 0;
                        }
                    } 
                    else if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                        //  网卡发送缓冲区满了！完美！我们保留 current_offset，直接 break！
                        // 退出当前观众的循环，等下一轮再继续发他剩下的数据！
                        break; 
                    } 
                    else {
                        //  网络彻底断开 (比如 VLC 被直接关闭)
                        viewer->active = false;
                        dead_fds.push_back(viewer->fd);
                        break;
                    }
                }
            }
        }

        // 4. 打扫战场，踢掉掉线的人
        for (int fd : dead_fds) {
            removeViewer(fd);
        }
    }
}

void TcpServer::start() {
    struct epoll_event events[1024];
// // Linux 底层对 epoll_event 的定义：
// struct epoll_event {
//     uint32_t events;      // 监控的事件类型 (比如 EPOLLIN 代表有数据可读)
//     epoll_data_t data;    // 附带的用户数据 (通常用来存文件描述符 fd)
// };

// // epoll_data_t 是一个联合体(union)，最常用的就是里面的 fd
// typedef union epoll_data {
//     void *ptr;
//     int fd;               
//     uint32_t u32;
//     uint64_t u64;
// } epoll_data_t;


    while (is_running) {
//  
        int nfds = epoll_wait(epoll_fd, events, 1024, -1);

        if (nfds < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << " epoll_wait 失败！" << std::endl;
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == server_fd) {
                // 处理新连接
                struct sockaddr_in client_addr;
                socklen_t client_addr_len = sizeof(client_addr);

                while (true) {
                    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
                    if (client_fd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        }
                        break;
                    }

                    TcpServer::setNonBlocking(client_fd);
                    struct epoll_event client_event;
                    client_event.events = EPOLLIN | EPOLLRDHUP;
                    client_event.data.fd = client_fd;
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event);

                    // [修改说明] 新连接先不急着认身份，先放进 pending_probe，等收到首包再决定它是 VLC 还是摄像头。
                    {
                        std::lock_guard<std::mutex> lock(producers_mtx);
                        pending_probe[client_fd] = {};
                    }
                }
            } else {
                int active_client_fd = events[i].data.fd;

                if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                    removeViewer(active_client_fd);
                    continue;
                }

                char buffer[65536];
                
                //  绝对的单线程读取，保证时序严丝合缝！
                ssize_t bytes_read = read(active_client_fd, buffer, sizeof(buffer));

                if (bytes_read > 0) {
                    bool is_known_viewer = false;
                    {
                        std::lock_guard<std::mutex> lock(viewers_mtx);
                        is_known_viewer = viewers.find(active_client_fd) != viewers.end();
                    }

                    // [修改说明] 观众连接除首次 GET 外，不需要再读取后续业务数据，直接忽略即可。
                    if (is_known_viewer) {
                        continue;
                    }

                    bool is_known_producer = false;
                    {
                        std::lock_guard<std::mutex> lock(producers_mtx);
                        is_known_producer = producers.find(active_client_fd) != producers.end();
                    }

                    if (is_known_producer) {
                        if (!handleProducerBytes(active_client_fd,
                                                 reinterpret_cast<uint8_t*>(buffer),
                                                 static_cast<size_t>(bytes_read))) {
                            removeViewer(active_client_fd);
                        }
                        continue;
                    }

                    // [修改说明] 首包判定逻辑：
                    // [修改说明] 前 4 字节如果是 "GET "，说明是 VLC；
                    // [修改说明] 否则就当作摄像头发来的“长度前缀流”。
                    bool become_viewer = false;
                    std::vector<uint8_t> first_producer_chunk;

                    {
                        std::lock_guard<std::mutex> lock(producers_mtx);
                        auto& probe = pending_probe[active_client_fd];
                        probe.insert(probe.end(), buffer, buffer + bytes_read);

                        if (probe.size() >= PROBE_BYTES) {
                            if (memcmp(probe.data(), "GET ", 4) == 0) {
                                pending_probe.erase(active_client_fd);
                                become_viewer = true;
                            } else {
                                first_producer_chunk.swap(probe);
                                pending_probe.erase(active_client_fd);
                                producers[active_client_fd] = ProducerState{};
                            }
                        }
                    }

                    if (become_viewer) {
                        handleViewerRequest(active_client_fd);
                        continue;
                    }

                    if (!first_producer_chunk.empty()) {
                        if (!handleProducerBytes(active_client_fd,
                                                 first_producer_chunk.data(),
                                                 first_producer_chunk.size())) {
                            removeViewer(active_client_fd);
                        }
                    }
                } else if (bytes_read == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                    // 无论是观众还是摄像头掉线，统一安全清理
                    removeViewer(active_client_fd);
                }
            }
        }
    }
}

TcpServer::~TcpServer() {
    is_running = false;
    sender_cv.notify_all();
    if (sender_thread.joinable()) sender_thread.join();

    if (server_fd != -1) close(server_fd);
    if (epoll_fd != -1) close(epoll_fd);
    std::cout << " 网关安全关机完毕。" << std::endl;
}
