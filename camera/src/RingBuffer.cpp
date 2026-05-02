#include "RingBuffer.h"
#include <cstring>
#include <iostream>

RingBuffer::RingBuffer(int cap, size_t f_size) 
    : capacity(cap), frame_size(f_size), write_idx(0), read_idx(0), count(0) {
    // 提前在内存里开辟好所有的空间，不在运行中 new/delete
    buffer.resize(capacity, std::vector<uint8_t>(frame_size));
    std::cout << " 环形缓冲区已建立 容量: " << capacity << " 帧" << std::endl;
}

RingBuffer::~RingBuffer() {}

void RingBuffer::push(const uint8_t* data, size_t size) {
    std::lock_guard<std::mutex> lock(mtx); // 进门先上锁，出作用域自动解锁

    // 把原始画面拷贝进水池的当前位置
    std::memcpy(buffer[write_idx].data(), data, size);

    // 写指针往前走一步，到头了就绕回 0 (转圈圈)
    write_idx = (write_idx + 1) % capacity;

    if (count < capacity) {
        count++; // 水池没满，库存 +1
    } else {
        // 丢帧策略：水池满了！
        // 强行把读指针往前推一格（等于把最老的那帧画面当垃圾扔了）
        read_idx = (read_idx + 1) % capacity;
        std::cerr << " 网络拥堵警告！触发丢帧保护，丢弃最老的一帧！" << std::endl;
    }
    cv.notify_one(); 
}

void RingBuffer::pop(std::vector<uint8_t>& out_data) {
    std::unique_lock<std::mutex> lock(mtx); // 上锁

    cv.wait(lock, [this]() { return count > 0; });

    // 被唤醒了！说明有图了，把图拿出来，
    // assign 会自动向系统申请足够内存，并自动清空旧垃圾，比 直接=赋值好
    out_data.assign(buffer[read_idx].begin(), buffer[read_idx].end());

    // 读指针往前走一步
    read_idx = (read_idx + 1) % capacity;
    count--; // 库存 -1

}