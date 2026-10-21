#pragma once
#include "IOLimits.hpp"
#include <atomic>
#include <memory>

#ifndef RCT_IO_MAX_ASYNC_PAYLOAD_BYTES
#define RCT_IO_MAX_ASYNC_PAYLOAD_BYTES (256ULL * 1024 * 1024)
#endif
namespace rct_io {
// Separate process-wide bounds for copied native inputs and pending native
// results. These count payload bytes, NOT total process/JS/platform heap usage.
class AsyncPayloadLease {
public:
    enum class Kind { Input, Output };
    static constexpr size_t limit = RCT_IO_MAX_ASYNC_PAYLOAD_BYTES;
private:
    inline static std::atomic<size_t> inputs_{0}, outputs_{0};
    Kind kind_;
    size_t bytes_ = 0;
    std::atomic<size_t>& counter() { return kind_ == Kind::Input ? inputs_ : outputs_; }
public:
    explicit AsyncPayloadLease(Kind kind) : kind_(kind) {}
    ~AsyncPayloadLease() { counter().fetch_sub(bytes_); }
    AsyncPayloadLease(const AsyncPayloadLease&) = delete;
    AsyncPayloadLease& operator=(const AsyncPayloadLease&) = delete;
    void add(size_t bytes) {
        auto& total = counter();
        size_t current = total.load();
        do {
            if (bytes > limit || current > limit - bytes) throw std::length_error("Native asynchronous payload budget exceeded; use streaming or lower concurrency");
        } while (!total.compare_exchange_weak(current, current + bytes));
        bytes_ += bytes;
    }
    static size_t used(Kind kind) { return (kind == Kind::Input ? inputs_ : outputs_).load(); }
};
} // namespace rct_io
