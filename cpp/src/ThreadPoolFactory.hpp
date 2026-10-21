#pragma once
#include "BS_thread_pool.hpp"
#include <atomic>
#include <memory>
#include <stdexcept>
#ifndef RCT_IO_MAX_WORKER_THREADS
#define RCT_IO_MAX_WORKER_THREADS 64
#endif
namespace rct_io {
inline std::atomic<size_t> liveWorkerThreads{0};
inline std::shared_ptr<BS::thread_pool<>> makeThreadPool(size_t count) {
    if (count == 0 || count > 32) throw std::invalid_argument("Thread count must be in 1..32");
    size_t current = liveWorkerThreads.load();
    do {
        if (count > RCT_IO_MAX_WORKER_THREADS || current > RCT_IO_MAX_WORKER_THREADS - count)
            throw std::runtime_error("Process-wide I/O worker limit exceeded; reuse an existing FSContext");
    } while (!liveWorkerThreads.compare_exchange_weak(current, current + count));
    BS::thread_pool<>* pool;
    try { pool = new BS::thread_pool<>(static_cast<unsigned int>(count)); }
    catch (...) { liveWorkerThreads.fetch_sub(count); throw; }
    // shared_ptr invokes this deleter on control-block allocation failure too.
    return std::shared_ptr<BS::thread_pool<>>(pool, [count](auto* value) {
        delete value;
        liveWorkerThreads.fetch_sub(count);
    });
}
} // namespace rct_io
