#include "AppConfig.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

// 匿名命名空间，使内部函数只在本cpp可见
namespace {

//读取环境变量，不存在使用默认值
int readIntEnv(const char* key, int default_value) {
    const char* value = std::getenv(key);
    if (value == nullptr || *value == '\0') {
        return default_value;
    }
    return std::atoi(value);
}

float readFloatEnv(const char* key, float default_value) {
    const char* value = std::getenv(key);
    if (value == nullptr || *value == '\0') {
        return default_value;
    }
// char--double--float
    return static_cast<float>(std::atof(value));
}

bool readBoolEnv(const char* key, bool default_value) {
    const char* value = std::getenv(key);
    if (value == nullptr || *value == '\0') {
        return default_value;
    }

    const std::string text(value);
    return text == "1" || text == "true" || text == "TRUE" || text == "yes";
}

std::string readStringEnv(const char* key, const std::string& default_value) {
    const char* value = std::getenv(key);
    if (value == nullptr) {
        return default_value;
    }
    return value;
}

}  // namespace

AppConfig AppConfig::fromEnvironment() {
    AppConfig config;
    config.sensor_id = readIntEnv("AICAM_SENSOR_ID", config.sensor_id);
    config.fps = readIntEnv("AICAM_FPS", config.fps);
    config.iq_dir = readStringEnv("AICAM_IQ_DIR", config.iq_dir);
    config.video_device = readStringEnv("AICAM_VIDEO_DEVICE", config.video_device);
    config.video_width = readIntEnv("AICAM_WIDTH", config.video_width);
    config.video_height = readIntEnv("AICAM_HEIGHT", config.video_height);
    config.bitrate_kbps = readIntEnv("AICAM_BITRATE_KBPS", config.bitrate_kbps);
    config.gop = readIntEnv("AICAM_GOP", config.fps);

    config.gateway_ip = readStringEnv("AICAM_GATEWAY_IP", config.gateway_ip);
    config.gateway_port = readIntEnv("AICAM_GATEWAY_PORT", config.gateway_port);

    config.encode_queue_capacity = readIntEnv("AICAM_ENCODE_QUEUE", config.encode_queue_capacity);
    config.ai_queue_capacity = readIntEnv("AICAM_AI_QUEUE", config.ai_queue_capacity);
    config.verbose_encode_log = readBoolEnv("AICAM_VERBOSE_ENCODE", config.verbose_encode_log);

    config.event_jsonl_path = readStringEnv("AICAM_EVENT_JSONL", config.event_jsonl_path);
    config.metrics_jsonl_path = readStringEnv("AICAM_METRICS_JSONL", config.metrics_jsonl_path);
    config.metrics_interval_sec = readIntEnv("AICAM_METRICS_INTERVAL", config.metrics_interval_sec);
    config.overlay_enable = readBoolEnv("AICAM_OVERLAY_ENABLE", config.overlay_enable);
    config.overlay_box_thickness = readIntEnv("AICAM_OVERLAY_THICKNESS", config.overlay_box_thickness);
    config.overlay_stale_frames = readIntEnv("AICAM_OVERLAY_STALE_FRAMES", config.overlay_stale_frames);

    config.ai.model_path = readStringEnv("AICAM_RKNN_MODEL", "");
    config.ai.labels_path = readStringEnv("AICAM_LABELS", "");
    config.ai.score_threshold = readFloatEnv("AICAM_SCORE_THRESHOLD", config.ai.score_threshold);
    config.ai.nms_threshold = readFloatEnv("AICAM_NMS_THRESHOLD", config.ai.nms_threshold);
    config.ai.infer_every_n_frames = readIntEnv("AICAM_INFER_EVERY_N", config.ai.infer_every_n_frames);
    config.ai.max_results = readIntEnv("AICAM_MAX_RESULTS", config.ai.max_results);
    config.ai.has_objectness = readBoolEnv("AICAM_HAS_OBJECTNESS", config.ai.has_objectness);
    config.ai.box_format = readStringEnv("AICAM_BOX_FORMAT", config.ai.box_format);
    config.ai.enabled = readBoolEnv("AICAM_AI_ENABLE", !config.ai.model_path.empty());

// 防止用户配置了不合理的值导致程序异常，做一些兜底修正
    if (config.metrics_interval_sec <= 0) {
        config.metrics_interval_sec = 5;
    }
    if (config.fps <= 0) {
        config.fps = 30;
    }
    if (config.video_device.empty()) {
        config.video_device = "/dev/video13";
    }
    if (config.video_width <= 0) {
        config.video_width = 1920;
    }
    if (config.video_height <= 0) {
        config.video_height = 1080;
    }
    if (config.bitrate_kbps <= 0) {
        config.bitrate_kbps = 4096;
    }
    if (config.gop <= 0) {
        config.gop = config.fps;
    }
    if (config.gateway_port <= 0 || config.gateway_port > 65535) {
        config.gateway_port = 8080;
    }
    if (config.encode_queue_capacity <= 0) {
        config.encode_queue_capacity = 5;
    }
    if (config.ai_queue_capacity <= 0) {
        config.ai_queue_capacity = 3;
    }
    if (config.overlay_box_thickness <= 0) {
        config.overlay_box_thickness = 4;
    }
    if (config.overlay_stale_frames <= 0) {
        config.overlay_stale_frames = std::max(30, config.ai.infer_every_n_frames * 3);
    }
    if (config.ai.infer_every_n_frames <= 0) {
        config.ai.infer_every_n_frames = 1;
    }

    return config;
}

void AppConfig::printSummary() const {
    std::cout << "===== AICAM Configuration =====" << std::endl;
    std::cout << "sensor_id=" << sensor_id
              << " fps=" << fps
              << " iq_dir=" << iq_dir
              << std::endl;
    std::cout << "video_device=" << video_device
              << " resolution=" << video_width << 'x' << video_height
              << " bitrate_kbps=" << bitrate_kbps
              << " gop=" << gop
              << std::endl;
    std::cout << "gateway=" << gateway_ip << ':' << gateway_port
              << " encode_queue=" << encode_queue_capacity
              << " ai_queue=" << ai_queue_capacity
              << std::endl;
    std::cout << "ai_enabled=" << (ai.enabled ? "true" : "false")
              << " model=" << (ai.model_path.empty() ? "<unset>" : ai.model_path)
              << " labels=" << (ai.labels_path.empty() ? "<unset>" : ai.labels_path)
              << " infer_every_n=" << ai.infer_every_n_frames
              << std::endl;
    std::cout << "event_jsonl=" << event_jsonl_path
              << " metrics_jsonl=" << metrics_jsonl_path
              << " metrics_interval=" << metrics_interval_sec << "s"
              << std::endl;
    std::cout << "overlay_enabled=" << (overlay_enable ? "true" : "false")
              << " overlay_thickness=" << overlay_box_thickness
              << " overlay_stale_frames=" << overlay_stale_frames
              << std::endl;
}
