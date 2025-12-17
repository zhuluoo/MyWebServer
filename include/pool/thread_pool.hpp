#pragma once
// filename: thread_pool.hpp

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

class ThreadPool {
public:
  explicit ThreadPool(size_t thread_num = 8);
  ~ThreadPool();
  ThreadPool(const ThreadPool &) = delete;
  auto operator=(const ThreadPool &) -> ThreadPool & = delete;

  void add_task(const std::function<void()> &&task);

private:
  struct Pool {
    std::mutex mtx;
    std::condition_variable cond;
    bool is_closed = false;
    std::queue<std::function<void()>> tasks;
    size_t thread_num = 0;
  };

  std::shared_ptr<Pool> pool_;
};
