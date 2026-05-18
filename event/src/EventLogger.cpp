#include "EventLogger.h"

#include "FileUtils.h"
#include "TimeUtils.h"

#include <iostream>

EventLogger::EventLogger(std::string jsonl_path)
    : jsonl_path_(std::move(jsonl_path)) {}

EventLogger::~EventLogger() {
    if (stream_.is_open()) {
        stream_.close();
    }
}

bool EventLogger::initialize() {
    if (!aicam::fileutil::ensureParentDirectory(jsonl_path_)) {
        std::cerr << "创建事件日志目录失败: " << jsonl_path_ << std::endl;
        return false;
    }

    stream_.open(jsonl_path_.c_str(), std::ios::out | std::ios::app);
    if (!stream_.is_open()) {
        std::cerr << "打开事件日志文件失败: " << jsonl_path_ << std::endl;
        return false;
    }

    return true;
}

// 写入一条ai事件日志
void EventLogger::write(const ai::AiResult& result) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 控制台打印
    std::cout << "[event] frame=" << result.frame_sequence
              << " detections=" << result.detections.size()
              << " infer_ms=" << result.inference_ms
              << std::endl;

    if (!stream_.is_open()) {
        return;
    }

    stream_ << '{'
            << "\"timestamp\":\"" << aicam::timeutil::formatUnixMicros(result.capture_time_us) << "\","
            << "\"frame_sequence\":" << result.frame_sequence << ','
            << "\"backend\":\"" << escapeJson(result.backend) << "\","
            << "\"preprocess_ms\":" << result.preprocess_ms << ','
            << "\"inference_ms\":" << result.inference_ms << ','
            << "\"postprocess_ms\":" << result.postprocess_ms << ','
            << "\"note\":\"" << escapeJson(result.note) << "\","
            << "\"detections\":[";

    // 遍历每个检测结果，写入类别ID、标签、置信度和坐标等信息
    for (size_t i = 0; i < result.detections.size(); ++i) {
        const auto& det = result.detections[i];
        if (i != 0) {
            stream_ << ',';
        }

        stream_ << '{'
                << "\"class_id\":" << det.class_id << ','
                << "\"label\":\"" << escapeJson(det.label) << "\","
                << "\"score\":" << det.score << ','
                << "\"x1\":" << det.x1 << ','
                << "\"y1\":" << det.y1 << ','
                << "\"x2\":" << det.x2 << ','
                << "\"y2\":" << det.y2
                << '}';
    }

    stream_ << "]}\n";
    // 立刻把缓冲区内容写入文件，避免程序崩溃时丢失日志
    stream_.flush();

}

// 用于把普通字符串转成 JSON 安全字符串
std::string EventLogger::escapeJson(const std::string& input) const {
    std::string output;
    output.reserve(input.size());

    for (char ch : input) {
        switch (ch) {
            case '\\':
                output += "\\\\";
                break;
            case '"':
                output += "\\\"";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            default:
                output.push_back(ch);
                break;
        }
    }

    return output;
}
