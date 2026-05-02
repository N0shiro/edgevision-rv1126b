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

void handleSignal(int) {
    g_running = false;
}

}  // namespace

int main() {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);

    AppConfig config = AppConfig::fromEnvironment();
    config.printSummary();

    EventLogger event_logger(config.event_jsonl_path);
    if (!event_logger.initialize()) {
        std::cerr << "事件日志模块初始化失败，将继续运行但不落盘事件。" << std::endl;
    }

    MetricsCollector metrics_collector;
    MetricsWriter metrics_writer(config.metrics_jsonl_path);
    if (!metrics_writer.initialize()) {
        std::cerr << "指标日志模块初始化失败，将继续运行但不落盘指标。" << std::endl;
    }

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

    std::unique_ptr<AiDetector> detector(new RknnDetector(config.ai));
    const bool ai_ready = detector->initialize();
    if (!ai_ready) {
        std::cout << "AI 模块未启用，本次运行仅保留视频链路与指标统计。" << std::endl;
    }

    const size_t frame_size = camera.getPackedFrameSize();

    std::thread capture_thread([&]() {
        std::vector<uint8_t> scratch(frame_size);
        uint64_t sequence = 0;

        while (g_running) {
            if (!camera.captureFrame(scratch.data())) {
                continue;
            }

            CapturedFrame frame;
            frame.sequence = sequence++;
            frame.capture_time_us = aicam::timeutil::nowUnixMicros();
            frame.width = camera.getWidth();
            frame.height = camera.getHeight();
            frame.nv12.assign(scratch.begin(), scratch.end());

            const bool infer_this_frame =
                ai_ready &&
                config.ai.enabled &&
                (frame.sequence % static_cast<uint64_t>(config.ai.infer_every_n_frames) == 0);

            CapturedFrame ai_frame;
            if (infer_this_frame) {
                ai_frame = frame;
            }

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

    std::thread encode_thread([&]() {
        FramedTcpClient client(config.gateway_ip, config.gateway_port);
        if (!client.connectOnce()) {
            g_running = false;
            encode_queue.stop();
            ai_queue.stop();
            return;
        }

        CapturedFrame frame;
        std::vector<uint8_t> h264_packet;

        while (g_running && encode_queue.waitAndPop(frame)) {
            const auto encode_begin = std::chrono::steady_clock::now();
            overlay.apply(frame);
            if (!encoder.encode(frame.nv12.data(), h264_packet)) {
                continue;
            }

            const double encode_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - encode_begin
            ).count();
            metrics_collector.recordEncode(encode_ms);

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

    std::thread ai_thread;
    if (ai_ready) {
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
