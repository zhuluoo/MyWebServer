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

// File overview: Implements ThreadPool worker lifecycle and task queue.

#include "pool/thread_pool.hpp"

namespace my_web_server {

ThreadPool::ThreadPool(size_t thread_num) : pool_(std::make_shared<Pool>()) {
  pool_->thread_num = thread_num;
  // Creat worker threads
  for (size_t i = 0; i < thread_num; ++i) {
    pool_->workers.emplace_back(std::thread([pool = pool_]() {
      while (true) {
        std::function<void()> task;
        {
          std::unique_lock<std::mutex> lock(pool->mtx);
          // Wait until there is a task or the pool is closed
          pool->cond.wait(lock, [pool]() {
            return pool->is_closed || !pool->tasks.empty();
          });

          if (pool->is_closed && pool->tasks.empty()) {
            return;
          }

          task = std::move(pool->tasks.front());
          pool->tasks.pop();
        }
        task();
      }
    }));
  }
}

ThreadPool::~ThreadPool() {
  pool_->is_closed = true;
  pool_->cond.notify_all();
  for (std::thread& worker : pool_->workers) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

}  // namespace my_web_server
