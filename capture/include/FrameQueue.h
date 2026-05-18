#pragma once

#include "CapturedFrame.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

class FrameQueue {
public:
    // 创建一个帧队列并设置最大容量
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

    // 消费者线程用来阻塞取帧的函数，
    // 如果队列为空会等待，直到有帧可取或者队列被停止
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

    // 查询函数，查询丢失多少帧
    uint64_t droppedFrames() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_frames_;
    }

private:
    size_t capacity_;
    bool stopped_;
    uint64_t dropped_frames_;
    // 双端队列
    std::deque<CapturedFrame> queue_;
    // 保护队列和状态的互斥锁，
    // 需要可以在const函数中访问，所以mutable
    mutable std::mutex mutex_;
    // 用于等待的条件变量
    std::condition_variable cv_;
};
