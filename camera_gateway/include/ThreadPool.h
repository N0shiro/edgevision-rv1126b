#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>

class ThreadPool {
    private:
        std::vector<std::thread> workers; // 工作线程池
        std::queue<std::function<void()>> tasks; // 任务队列

        std::mutex queue_mutex; // 保护任务队列的互斥锁
        std::condition_variable condition; // 条件变量用于通知工作线程有新任务
        bool stop; // 标志线程池是否停止    

        
    public:
        ThreadPool(size_t threads);
        ~ThreadPool();

        // 添加新任务到线程池
        template<class F, class... Args>
        void enqueue(F&& f, Args&&... args);


};

#endif