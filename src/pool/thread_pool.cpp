#include "pool/thread_pool.hpp"
// filename: thread_pool.cpp

// Construcor of ThreadPool
ThreadPool::ThreadPool(size_t thread_num) : pool_(std::make_shared<Pool>()) {
  pool_->thread_num = thread_num;
  // Creat worker threads
  for (size_t i = 0; i < thread_num; ++i) {
    std::thread([pool = pool_]() {
      // Keep fetching and executing tasks
      while (true) {
        std::function<void()> task;
        {
          std::unique_lock<std::mutex> lock(pool->mtx);
          // Wait until there is a task or the pool is closed
          pool->cond.wait(lock, [pool]() {
            return pool->is_closed || !pool->tasks.empty();
          });
          // If the pool is closed and there are no tasks, exit the thread
          if (pool->is_closed && pool->tasks.empty()) {
            return;
          }
          // Fetch the next task
          task = std::move(pool->tasks.front());
          pool->tasks.pop();
        }
        task();
      }
    }).detach();
  }
}

// Add a new task to the thread pool
void ThreadPool::add_task(const std::function<void()> &&task) {
  std::unique_lock<std::mutex> lock(pool_->mtx);
  if (pool_->is_closed) {
    throw std::runtime_error("ThreadPool is closed. Cannot add new task.");
  }
  pool_->tasks.emplace(std::move(task));
  lock.unlock();
  // Notify one worker thread that a new task is available
  pool_->cond.notify_one();
}

// Destructor of ThreadPool
ThreadPool::~ThreadPool() {
  pool_->is_closed = true;
  // Notify all worker threads to exit
  pool_->cond.notify_all();
  // Wait for all tasks to complete
  while (true) {
    std::unique_lock<std::mutex> lock(pool_->mtx);
    if (pool_->tasks.empty()) {
      break;
    }
    lock.unlock();
    std::this_thread::yield();
  }
}
