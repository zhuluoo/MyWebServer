/*
 * Copyright (C) 2026 nate <176468367+zhuluoo@users.noreply.github.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// File overview: Defines ThreadPool for running tasks on worker threads.

#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>

namespace my_web_server {

class ThreadPool {
 public:
  explicit ThreadPool(size_t thread_num = 8);
  ~ThreadPool();
  ThreadPool(const ThreadPool&) = delete;
  auto operator=(const ThreadPool&) -> ThreadPool& = delete;

  template <typename F>
  void add_task(F&& task);

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

template <typename F>
void ThreadPool::add_task(F&& task) {
  std::unique_lock<std::mutex> lock(pool_->mtx);
  if (pool_->is_closed) {
    throw std::runtime_error("ThreadPool is closed. Cannot add new task.");
  }
  pool_->tasks.emplace(std::forward<F>(task));
  lock.unlock();
  pool_->cond.notify_one();
}

}  // namespace my_web_server
