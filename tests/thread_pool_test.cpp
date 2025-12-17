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
    std::cout << "\n\n---------------Main thread done-----------------------\n\n";
  }

  if (counter.load(std::memory_order_relaxed) == TASKS_NUMBER) {
    std::cout << "PASS: all tasks executed (" << TASKS_NUMBER << ")\n";
    return 0;
  }

  std::cerr << "FAIL: executed " << counter.load() << " of " << TASKS_NUMBER
            << " tasks\n";
  return 1;
}
