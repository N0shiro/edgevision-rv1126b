#pragma once
#include <vector>
#include <mutex>
#include <condition_variable>
#include <cstdint>

class RingBuffer {
public:
    // 构造函数：告诉水池最多装几帧 (capacity)，每帧多大 (frame_size)
    RingBuffer(int capacity, size_t frame_size);
    ~RingBuffer();

    // 生产者调用：强制塞入一帧（如果满了就触发核心的覆盖丢帧逻辑）
    void push(const uint8_t* data, size_t size);

    // 消费者调用：捞出一帧（如果没有数据则进入深度休眠，绝不占用 CPU）
    void pop(std::vector<uint8_t>& out_data);

private:
    std::vector<std::vector<uint8_t>> buffer; // 真正存放画面的水池 (二维数组)
    int capacity;
    size_t frame_size;
    
    int write_idx; // 生产者的写入游标
    int read_idx;  // 消费者的读取游标
    int count;     // 当前水池里有几帧

    std::mutex mtx;             // 保护水池的互斥锁
    std::condition_variable cv; // 控制消费者休眠和唤醒的条件变量
};