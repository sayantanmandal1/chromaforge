// chromaforge/thread_pool.hpp — a fixed-size worker pool with a task queue.
#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace chromaforge {

// A classic producer/consumer thread pool. Workers block on a condition
// variable until work is available; submit() enqueues a task and returns a
// std::future for its result. Destruction drains and joins all workers.
class ThreadPool {
public:
    explicit ThreadPool(size_t threads = 0) {
        if (threads == 0) {
            threads = std::thread::hardware_concurrency();
            if (threads == 0) threads = 4;
        }
        workers_.reserve(threads);
        for (size_t i = 0; i < threads; ++i) {
            workers_.emplace_back([this] { workerLoop(); });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(mtx_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& w : workers_) {
            if (w.joinable()) w.join();
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    size_t size() const { return workers_.size(); }

    // Enqueue a nullary callable; returns a future for its result.
    template <class F>
    auto submit(F&& f) -> std::future<decltype(f())> {
        using Result = decltype(f());
        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<F>(f));
        std::future<Result> fut = task->get_future();
        {
            std::unique_lock<std::mutex> lock(mtx_);
            if (stop_) throw std::runtime_error("submit() on stopped ThreadPool");
            tasks_.emplace([task] { (*task)(); });
        }
        cv_.notify_one();
        return fut;
    }

private:
    void workerLoop() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mtx_);
                cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool stop_ = false;
};

}  // namespace chromaforge
