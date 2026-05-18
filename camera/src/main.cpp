#include "AppConfig.h"
#include "AiDetector.h"
#include "CameraDevice.h"
#include "CapturedFrame.h"
#include "DetectionOverlay.h"
#include "EventLogger.h"
#include "FrameQueue.h"
#include "FramedTcpClient.h"
#include "H264Encoder.h"
#include "IspController.h"
#include "MetricsCollector.h"
#include "RknnDetector.h"
#include "TimeUtils.h"

#include <csignal>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_running(true);
// 原子布尔变量，保证线程安全

void handleSignal(int) {
    g_running = false;
}

}  // namespace

int main() {
    signal(SIGPIPE, SIG_IGN);
    // 忽略 SIGPIPE 信号，防止网络异常导致进程崩溃
    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);
    // 被用户中断或系统终止时，走自定义清理逻辑
    // 设置 g_running 为 false，触发安全退出流程

    // 配置加载与打印
    AppConfig config = AppConfig::fromEnvironment();
    config.printSummary();

    //创建事件日志记录器
    EventLogger event_logger(config.event_jsonl_path);
    if (!event_logger.initialize()) {
        std::cerr << "事件日志模块初始化失败，将继续运行但不落盘事件。" << std::endl;
    }

    // 创建程序运行状态统计器
    MetricsCollector metrics_collector;
    MetricsWriter metrics_writer(config.metrics_jsonl_path);
    if (!metrics_writer.initialize()) {
        std::cerr << "指标日志模块初始化失败，将继续运行但不落盘指标。" << std::endl;
    }

    // 控制并启动aiq/isp模块
    IspController isp(config.sensor_id, config.fps, config.iq_dir);
    if (!isp.start()) {
        std::cerr << "AIQ 未能启动，继续使用当前 ISP 状态。" << std::endl;
    }

    CameraDevice camera;
    camera.initCamera();
    camera.startStream();

    H264Encoder encoder(camera.getWidth(), camera.getHeight(), config.fps);
    FrameQueue encode_queue(static_cast<size_t>(config.encode_queue_capacity));
    FrameQueue ai_queue(static_cast<size_t>(config.ai_queue_capacity));
    DetectionOverlay overlay(
        config.overlay_enable,
        config.overlay_box_thickness,
        config.overlay_stale_frames
    );
    // 用aidetector（父类）类型创建一个rknn对象（子类）
    // 实现多态，用父类作为接口，方便未来换其他AI推理引擎
    std::unique_ptr<AiDetector> detector(new RknnDetector(config.ai));
    const bool ai_ready = detector->initialize();
    if (!ai_ready) {
        std::cout << "AI 模块未启用，本次运行仅保留视频链路与指标统计。" << std::endl;
    }

    const size_t frame_size = camera.getPackedFrameSize();

    // 创建采集线程 [&]表示 可以按引用访问 main 函数中的局部变量，
    // 如 camera、encode_queue、ai_queue、metrics_collector 等
    std::thread capture_thread([&]() {
        // 临时缓冲区
        std::vector<uint8_t> scratch(frame_size);
        // 帧序号
        uint64_t sequence = 0;
        // 主循环
        while (g_running) {
            // captureFrame函数按data返回的指针
            // 将采集一帧数据写入scratch缓冲区
            if (!camera.captureFrame(scratch.data())) {
                continue;
            }


            CapturedFrame frame;
            frame.sequence = sequence++;
            frame.capture_time_us = aicam::timeutil::nowUnixMicros();
            frame.width = camera.getWidth();
            frame.height = camera.getHeight();
            // scratch赋值给nv12
            frame.nv12.assign(scratch.begin(), scratch.end());

            // 判断当前帧是否需要进行AI推理，条件是：
            // ai模块配置成功、配置文件启用ai、按帧间隔抽帧推理
            const bool infer_this_frame =
                ai_ready &&
                config.ai.enabled &&
                (frame.sequence % static_cast<uint64_t>(config.ai.infer_every_n_frames) == 0);

            CapturedFrame ai_frame;
            if (infer_this_frame) {
                ai_frame = frame;
            }
            // 编码队列入队当前数据帧，如果队列已满则丢弃最旧一帧
            // 并记录一次丢帧事件
            if (encode_queue.push(std::move(frame))) {
                metrics_collector.recordEncodeQueueDrop();
            }

            if (infer_this_frame && ai_queue.push(std::move(ai_frame))) {
                metrics_collector.recordAiQueueDrop();
            }

            metrics_collector.recordCapture();
        }

        std::cout << "采集线程退出。" << std::endl;
    });

    // 编码加推流线程
    std::thread encode_thread([&]() {
        // 创建TCP客户端
        // 按配置里的ip和端口连接网关
        FramedTcpClient client(config.gateway_ip, config.gateway_port);
        if (!client.connectOnce()) {
            g_running = false;
            encode_queue.stop();
            ai_queue.stop();
            return;
        }

        CapturedFrame frame;
        std::vector<uint8_t> h264_packet;
        // 程序运行且读取到帧
        while (g_running && encode_queue.waitAndPop(frame)) {
            // 记录时间点，计算编码耗时
            const auto encode_begin = std::chrono::steady_clock::now();
            // 画框架
            overlay.apply(frame);
            // 根据图像数据指针frame.nv12.data()
            // 把图像编码成264，写到h264_packet里
            if (!encoder.encode(frame.nv12.data(), h264_packet)) {
                continue;
            }

            // 计算编码耗时
            const double encode_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - encode_begin
            ).count();
            metrics_collector.recordEncode(encode_ms);

            // 把编码好的数据发给网关
            if (!client.sendPacket(h264_packet)) {
                g_running = false;
                break;
            }

            metrics_collector.recordBytesSent(h264_packet.size());

            if (config.verbose_encode_log) {
                std::cout << "[stream] frame=" << frame.sequence
                          << " bytes=" << h264_packet.size()
                          << " encode_ms=" << encode_ms
                          << std::endl;
            }
        }

        client.closeConnection();
        std::cout << "编码推流线程退出。" << std::endl;
    });

    // 创建ai推理线程
    std::thread ai_thread;
    // 只有AI模块准备好且启用，才创建AI线程
    if (ai_ready) {
        // 真正创建线程
        ai_thread = std::thread([&]() {
            CapturedFrame frame;
            while (g_running && ai_queue.waitAndPop(frame)) {
                ai::AiResult result = detector->infer(frame);
                if (!result.valid) {
                    continue;
                }

                overlay.update(result);
                metrics_collector.recordAi(result.inference_ms);

                if (!result.detections.empty()) {
                    event_logger.write(result);
                    metrics_collector.recordEventWritten();
                }
            }

            std::cout << "AI 线程退出。" << std::endl;
        });
    }

    // 指标线程 循环每隔一段时间从指标收集器采样一次数据写入日志
    std::thread metrics_thread([&]() {
        while (g_running) {
            for (int i = 0; i < config.metrics_interval_sec * 10 && g_running; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            if (!g_running) {
                break;
            }

            metrics_writer.write(metrics_collector.sample());
        }

        metrics_writer.write(metrics_collector.sample());
        std::cout << "指标线程退出。" << std::endl;
    });

    std::cout << "AICAM 已启动，按 Ctrl+C 退出。" << std::endl;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    encode_queue.stop();
    ai_queue.stop();

    if (capture_thread.joinable()) {
        capture_thread.join();
    }
    if (encode_thread.joinable()) {
        encode_thread.join();
    }
    if (ai_thread.joinable()) {
        ai_thread.join();
    }
    if (metrics_thread.joinable()) {
        metrics_thread.join();
    }

    std::cout << "AICAM 进程已安全退出。" << std::endl;
    return 0;
}
