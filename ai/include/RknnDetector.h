#pragma once

#include "AiConfig.h"
#include "AiDetector.h"

#include <memory>
#include <string>
#include <vector>

class RknnDetector : public AiDetector {
public:
    explicit RknnDetector(AiConfig config);
    ~RknnDetector() override;

    bool initialize() override;
    bool isEnabled() const override;
    std::string backendName() const override;
    ai::AiResult infer(const CapturedFrame& frame) override;

private:
    struct Impl;

    AiConfig config_;
    std::unique_ptr<Impl> impl_;
    bool initialized_;
    std::string disabled_reason_;
};
