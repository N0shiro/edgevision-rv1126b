#pragma once

#include "CapturedFrame.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

class FrameQueue {
public:
    explicit FrameQueue(size_t capacity)
        : capacity_(capacity), stopped_(false), dropped_frames_(0) {}

    bool push(CapturedFrame frame) {
        std::lock_guard<std::mutex> lock(mutex_);

        bool dropped = false;
        if (queue_.size() >= capacity_) {
            queue_.pop_front();
            ++dropped_frames_;
            dropped = true;
        }

        queue_.push_back(std::move(frame));
        cv_.notify_one();
        return dropped;
    }

    bool waitAndPop(CapturedFrame& frame) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]() { return stopped_ || !queue_.empty(); });

        if (queue_.empty()) {
            return false;
        }

        frame = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    void stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        cv_.notify_all();
    }

    uint64_t droppedFrames() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_frames_;
    }

private:
    size_t capacity_;
    bool stopped_;
    uint64_t dropped_frames_;
    std::deque<CapturedFrame> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
};
