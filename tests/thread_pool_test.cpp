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

// File overview: Stress test for ThreadPool task execution.

#include "pool/thread_pool.hpp"

#include <atomic>
#include <iostream>

auto main() -> int {
  constexpr int TASKS_NUMBER = 1000;
  std::atomic<int> counter{0};

  {
    ThreadPool pool(8);
    for (int i = 0; i < TASKS_NUMBER; ++i) {
      pool.add_task([&counter]() {
        int ctr = counter.fetch_add(1, std::memory_order_relaxed);
        std::cout << "Task " << ctr + 1
                  << " is taken by thread: " << std::this_thread::get_id()
                  << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
      });
    }
    // Let destructor wait for tasks to finish when pool goes out of scope
    std::cout
        << "\n\n---------------Main thread done-----------------------\n\n";
  }

  if (counter.load(std::memory_order_relaxed) == TASKS_NUMBER) {
    std::cout << "PASS: all tasks executed (" << TASKS_NUMBER << ")\n";
    return 0;
  }

  std::cerr << "FAIL: executed " << counter.load() << " of " << TASKS_NUMBER
            << " tasks\n";
  return 1;
}
