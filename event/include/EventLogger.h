#pragma once

#include "AiTypes.h"

#include <fstream>
#include <mutex>
#include <string>

class EventLogger {
public:
    explicit EventLogger(std::string jsonl_path);
    ~EventLogger();

    bool initialize();
    void write(const ai::AiResult& result);

private:
    std::string escapeJson(const std::string& input) const;

    std::string jsonl_path_;
    std::ofstream stream_;
    std::mutex mutex_;
};
