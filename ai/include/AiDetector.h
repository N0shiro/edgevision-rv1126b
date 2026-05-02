#pragma once

#include "AiTypes.h"
#include "CapturedFrame.h"

#include <string>

class AiDetector {
public:
    virtual ~AiDetector() = default;

    virtual bool initialize() = 0;
    virtual bool isEnabled() const = 0;
    virtual std::string backendName() const = 0;
    virtual ai::AiResult infer(const CapturedFrame& frame) = 0;
};
