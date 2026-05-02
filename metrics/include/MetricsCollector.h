#pragma once

#include <atomic>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>

struct MetricsSnapshot {
    uint64_t timestamp_us = 0;
    double capture_fps = 0.0;
    double encode_fps = 0.0;
    double ai_fps = 0.0;
    double average_encode_ms = 0.0;
    double average_inference_ms = 0.0;
    double cpu_percent = 0.0;
    double rss_mb = 0.0;
    uint64_t bytes_sent = 0;
    uint64_t events_written = 0;
    uint64_t encode_queue_drops = 0;
    uint64_t ai_queue_drops = 0;
};

class MetricsCollector {
public:
    MetricsCollector();

    void recordCapture();
    void recordEncode(double encode_ms);
    void recordAi(double inference_ms);
    void recordBytesSent(uint64_t bytes);
    void recordEventWritten();
    void recordEncodeQueueDrop();
    void recordAiQueueDrop();

    MetricsSnapshot sample();

private:
    struct ProcSample {
        uint64_t total_jiffies = 0;
        uint64_t process_jiffies = 0;
        uint64_t rss_pages = 0;
        bool valid = false;
    };

    ProcSample readProcSample() const;

    std::atomic<uint64_t> captured_frames_;
    std::atomic<uint64_t> encoded_frames_;
    std::atomic<uint64_t> ai_frames_;
    std::atomic<uint64_t> bytes_sent_;
    std::atomic<uint64_t> events_written_;
    std::atomic<uint64_t> encode_queue_drops_;
    std::atomic<uint64_t> ai_queue_drops_;
    std::atomic<uint64_t> encode_time_us_total_;
    std::atomic<uint64_t> inference_time_us_total_;

    uint64_t last_capture_frames_;
    uint64_t last_encoded_frames_;
    uint64_t last_ai_frames_;
    uint64_t last_encode_time_us_total_;
    uint64_t last_inference_time_us_total_;
    uint64_t last_sample_time_us_;
    ProcSample last_proc_sample_;
    long page_size_;
    long clock_ticks_;
    long cpu_cores_;
    std::mutex sample_mutex_;
};

class MetricsWriter {
public:
    explicit MetricsWriter(std::string jsonl_path);
    ~MetricsWriter();

    bool initialize();
    void write(const MetricsSnapshot& snapshot);

private:
    std::string jsonl_path_;
    std::ofstream stream_;
    std::mutex mutex_;
};
