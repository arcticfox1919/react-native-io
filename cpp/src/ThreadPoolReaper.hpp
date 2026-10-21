#pragma once
#include "BS_thread_pool.hpp"
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace rct_io {
// Pool destruction joins workers. Never join an I/O worker from itself, or
// block the JS thread waiting for a slow file/network task during GC.
class ThreadPoolReaper {
  struct Retirement {
    std::shared_ptr<BS::thread_pool<>> pool;
    std::function<void()> afterJoin;
  };
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<Retirement> retirements_;
  std::thread thread_;
  ThreadPoolReaper() : thread_([this] {
    for (;;) {
      Retirement retirement;
      {
        std::unique_lock lock(mutex_);
        wake_.wait(lock, [this] { return !retirements_.empty(); });
        retirement = std::move(retirements_.front());
        retirements_.pop_front();
      }
      // Destroying the pool joins all workers. Cleanup then runs after every
      // already-submitted task has stopped touching its resources.
      retirement.pool.reset();
      if (retirement.afterJoin) {
        try { retirement.afterJoin(); } catch (...) {}
      }
    }
  }) {}
 public:
  // Native modules are process-lifetime code on Android and iOS. Keeping this
  // tiny service alive deliberately avoids static-destruction races where a
  // late HostObject tries to retire a pool after the reaper was destroyed.
  static ThreadPoolReaper& instance() {
    static ThreadPoolReaper* reaper = new ThreadPoolReaper();
    return *reaper;
  }
  void retire(std::shared_ptr<BS::thread_pool<>> pool,
              std::function<void()> afterJoin = {}) {
    if (!pool && !afterJoin) return;
    {
      std::lock_guard lock(mutex_);
      retirements_.push_back({std::move(pool), std::move(afterJoin)});
    }
    wake_.notify_one();
  }
  ThreadPoolReaper(const ThreadPoolReaper&) = delete;
  ThreadPoolReaper& operator=(const ThreadPoolReaper&) = delete;
};
} // namespace rct_io
