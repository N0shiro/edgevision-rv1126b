#pragma once

#include "AiConfig.h"

#include <string>

struct AppConfig {
    int sensor_id = 0;
    int fps = 30;
    std::string iq_dir = "/etc/iqfiles";

    std::string gateway_ip = "127.0.0.1";
    int gateway_port = 8080;

    int encode_queue_capacity = 5;
    int ai_queue_capacity = 3;
    bool verbose_encode_log = false;

    std::string event_jsonl_path = "logs/events.jsonl";
    std::string metrics_jsonl_path = "logs/metrics.jsonl";
    int metrics_interval_sec = 5;

    bool overlay_enable = true;
    int overlay_box_thickness = 4;
    int overlay_stale_frames = 30;

    AiConfig ai;

    static AppConfig fromEnvironment();
    void printSummary() const;
};
