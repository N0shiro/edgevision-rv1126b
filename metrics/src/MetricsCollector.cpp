#include "MetricsCollector.h"

#include "FileUtils.h"
#include "TimeUtils.h"

#include <sys/types.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

double safeRate(uint64_t current, uint64_t previous, double interval_seconds) {
    if (interval_seconds <= 0.0 || current < previous) {
        return 0.0;
    }
    return static_cast<double>(current - previous) / interval_seconds;
}

}  // namespace

MetricsCollector::MetricsCollector()
    : captured_frames_(0),
      encoded_frames_(0),
      ai_frames_(0),
      bytes_sent_(0),
      events_written_(0),
      encode_queue_drops_(0),
      ai_queue_drops_(0),
      encode_time_us_total_(0),
      inference_time_us_total_(0),
      last_capture_frames_(0),
      last_encoded_frames_(0),
      last_ai_frames_(0),
      last_encode_time_us_total_(0),
      last_inference_time_us_total_(0),
      last_sample_time_us_(aicam::timeutil::nowUnixMicros()),
      page_size_(sysconf(_SC_PAGESIZE)),
      clock_ticks_(sysconf(_SC_CLK_TCK)),
      cpu_cores_(sysconf(_SC_NPROCESSORS_ONLN)) {
    last_proc_sample_ = readProcSample();
}

void MetricsCollector::recordCapture() {
    captured_frames_.fetch_add(1, std::memory_order_relaxed);
}

void MetricsCollector::recordEncode(double encode_ms) {
    encoded_frames_.fetch_add(1, std::memory_order_relaxed);
    encode_time_us_total_.fetch_add(
        static_cast<uint64_t>(encode_ms * 1000.0),
        std::memory_order_relaxed
    );
}

void MetricsCollector::recordAi(double inference_ms) {
    ai_frames_.fetch_add(1, std::memory_order_relaxed);
    inference_time_us_total_.fetch_add(
        static_cast<uint64_t>(inference_ms * 1000.0),
        std::memory_order_relaxed
    );
}

void MetricsCollector::recordBytesSent(uint64_t bytes) {
    bytes_sent_.fetch_add(bytes, std::memory_order_relaxed);
}

void MetricsCollector::recordEventWritten() {
    events_written_.fetch_add(1, std::memory_order_relaxed);
}

void MetricsCollector::recordEncodeQueueDrop() {
    encode_queue_drops_.fetch_add(1, std::memory_order_relaxed);
}

void MetricsCollector::recordAiQueueDrop() {
    ai_queue_drops_.fetch_add(1, std::memory_order_relaxed);
}

MetricsSnapshot MetricsCollector::sample() {
    std::lock_guard<std::mutex> lock(sample_mutex_);

    MetricsSnapshot snapshot;
    snapshot.timestamp_us = aicam::timeutil::nowUnixMicros();
    const double interval_seconds = static_cast<double>(snapshot.timestamp_us - last_sample_time_us_) / 1000000.0;

    const uint64_t capture_frames = captured_frames_.load(std::memory_order_relaxed);
    const uint64_t encoded_frames = encoded_frames_.load(std::memory_order_relaxed);
    const uint64_t ai_frames = ai_frames_.load(std::memory_order_relaxed);
    const uint64_t encode_time_us_total = encode_time_us_total_.load(std::memory_order_relaxed);
    const uint64_t inference_time_us_total = inference_time_us_total_.load(std::memory_order_relaxed);

    snapshot.capture_fps = safeRate(capture_frames, last_capture_frames_, interval_seconds);
    snapshot.encode_fps = safeRate(encoded_frames, last_encoded_frames_, interval_seconds);
    snapshot.ai_fps = safeRate(ai_frames, last_ai_frames_, interval_seconds);

    const uint64_t encode_frames_delta = encoded_frames - last_encoded_frames_;
    const uint64_t ai_frames_delta = ai_frames - last_ai_frames_;

    if (encode_frames_delta > 0) {
        snapshot.average_encode_ms = static_cast<double>(
            encode_time_us_total - last_encode_time_us_total_
        ) / static_cast<double>(encode_frames_delta) / 1000.0;
    }

    if (ai_frames_delta > 0) {
        snapshot.average_inference_ms = static_cast<double>(
            inference_time_us_total - last_inference_time_us_total_
        ) / static_cast<double>(ai_frames_delta) / 1000.0;
    }

    snapshot.bytes_sent = bytes_sent_.load(std::memory_order_relaxed);
    snapshot.events_written = events_written_.load(std::memory_order_relaxed);
    snapshot.encode_queue_drops = encode_queue_drops_.load(std::memory_order_relaxed);
    snapshot.ai_queue_drops = ai_queue_drops_.load(std::memory_order_relaxed);

    const ProcSample proc_sample = readProcSample();
    if (proc_sample.valid && last_proc_sample_.valid) {
        const uint64_t total_delta = proc_sample.total_jiffies - last_proc_sample_.total_jiffies;
        const uint64_t proc_delta = proc_sample.process_jiffies - last_proc_sample_.process_jiffies;
        if (total_delta > 0 && cpu_cores_ > 0) {
            snapshot.cpu_percent = static_cast<double>(proc_delta) /
                                   static_cast<double>(total_delta) *
                                   100.0 * static_cast<double>(cpu_cores_);
        }
        snapshot.rss_mb = static_cast<double>(proc_sample.rss_pages * page_size_) / 1024.0 / 1024.0;
    }

    last_capture_frames_ = capture_frames;
    last_encoded_frames_ = encoded_frames;
    last_ai_frames_ = ai_frames;
    last_encode_time_us_total_ = encode_time_us_total;
    last_inference_time_us_total_ = inference_time_us_total;
    last_sample_time_us_ = snapshot.timestamp_us;
    last_proc_sample_ = proc_sample;

    return snapshot;
}

MetricsCollector::ProcSample MetricsCollector::readProcSample() const {
    ProcSample sample;

    {
        std::ifstream stat_file("/proc/stat");
        std::string cpu_tag;
        uint64_t user = 0;
        uint64_t nice = 0;
        uint64_t system = 0;
        uint64_t idle = 0;
        uint64_t iowait = 0;
        uint64_t irq = 0;
        uint64_t softirq = 0;
        uint64_t steal = 0;

        if (!(stat_file >> cpu_tag >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal)) {
            return sample;
        }

        sample.total_jiffies = user + nice + system + idle + iowait + irq + softirq + steal;
    }

    {
        std::ifstream stat_file("/proc/self/stat");
        std::string line;
        if (!std::getline(stat_file, line)) {
            return sample;
        }

        const size_t close_paren = line.rfind(')');
        if (close_paren == std::string::npos || close_paren + 2 >= line.size()) {
            return sample;
        }

        std::istringstream iss(line.substr(close_paren + 2));
        char state = '\0';
        iss >> state;

        uint64_t ignored_u64 = 0;
        long ignored_long = 0;
        uint64_t utime = 0;
        uint64_t stime = 0;
        uint64_t vsize = 0;
        long rss = 0;

        for (int field = 4; field <= 13; ++field) {
            if (!(iss >> ignored_u64)) {
                return sample;
            }
        }

        if (!(iss >> utime >> stime)) {
            return sample;
        }

        // After reading fields 14 and 15 (utime/stime), skip fields 16-22 and
        // then read fields 23 (vsize) and 24 (rss). The previous logic skipped
        // one field too many and accidentally treated rsslim as rss.
        for (int field = 16; field <= 22; ++field) {
            if (!(iss >> ignored_long)) {
                return sample;
            }
        }

        if (!(iss >> vsize >> rss)) {
            return sample;
        }

        sample.process_jiffies = utime + stime;
        sample.rss_pages = rss > 0 ? static_cast<uint64_t>(rss) : 0;
    }

    sample.valid = true;
    return sample;
}

MetricsWriter::MetricsWriter(std::string jsonl_path)
    : jsonl_path_(std::move(jsonl_path)) {}

MetricsWriter::~MetricsWriter() {
    if (stream_.is_open()) {
        stream_.close();
    }
}

bool MetricsWriter::initialize() {
    if (!aicam::fileutil::ensureParentDirectory(jsonl_path_)) {
        std::cerr << "创建指标日志目录失败: " << jsonl_path_ << std::endl;
        return false;
    }

    stream_.open(jsonl_path_.c_str(), std::ios::out | std::ios::app);
    if (!stream_.is_open()) {
        std::cerr << "打开指标日志文件失败: " << jsonl_path_ << std::endl;
        return false;
    }

    return true;
}

void MetricsWriter::write(const MetricsSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::cout << "[metrics] capture_fps=" << snapshot.capture_fps
              << " encode_fps=" << snapshot.encode_fps
              << " ai_fps=" << snapshot.ai_fps
              << " cpu=" << snapshot.cpu_percent
              << "% rss=" << snapshot.rss_mb
              << "MB" << std::endl;

    if (!stream_.is_open()) {
        return;
    }

    stream_ << '{'
            << "\"timestamp\":\"" << aicam::timeutil::formatUnixMicros(snapshot.timestamp_us) << "\","
            << "\"capture_fps\":" << snapshot.capture_fps << ','
            << "\"encode_fps\":" << snapshot.encode_fps << ','
            << "\"ai_fps\":" << snapshot.ai_fps << ','
            << "\"average_encode_ms\":" << snapshot.average_encode_ms << ','
            << "\"average_inference_ms\":" << snapshot.average_inference_ms << ','
            << "\"cpu_percent\":" << snapshot.cpu_percent << ','
            << "\"rss_mb\":" << snapshot.rss_mb << ','
            << "\"bytes_sent\":" << snapshot.bytes_sent << ','
            << "\"events_written\":" << snapshot.events_written << ','
            << "\"encode_queue_drops\":" << snapshot.encode_queue_drops << ','
            << "\"ai_queue_drops\":" << snapshot.ai_queue_drops
            << "}\n";
    stream_.flush();
}
