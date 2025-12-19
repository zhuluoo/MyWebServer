#pragma once
// filename: thread_pool.hpp

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>

class ThreadPool {
public:
  explicit ThreadPool(size_t thread_num = 8);
  ~ThreadPool();
  ThreadPool(const ThreadPool &) = delete;
  auto operator=(const ThreadPool &) -> ThreadPool & = delete;

  template <typename F> void add_task(F &&task);

private:
  struct Pool {
    std::mutex mtx;
    std::condition_variable cond;
    bool is_closed = false;
    size_t thread_num = 0;
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
  };

  std::shared_ptr<Pool> pool_;
};

// Add a new task to the thread pool
template <typename F> void ThreadPool::add_task(F &&task) {
  std::unique_lock<std::mutex> lock(pool_->mtx);
  if (pool_->is_closed) {
    throw std::runtime_error("ThreadPool is closed. Cannot add new task.");
  }
  pool_->tasks.emplace(std::forward<F>(task));
  lock.unlock();
  // Notify one worker thread that a new task is available
  pool_->cond.notify_one();
}
