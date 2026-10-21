#pragma once
#include <cstddef>
#include <stdexcept>
#include <string>

#ifndef RCT_IO_MAX_BUFFER_BYTES
#define RCT_IO_MAX_BUFFER_BYTES (256ULL * 1024 * 1024)
#endif
namespace rct_io {
inline constexpr size_t maxBufferBytes = RCT_IO_MAX_BUFFER_BYTES;
inline void checkBufferSize(size_t size) {
  if (size > maxBufferBytes) throw std::length_error("I/O buffer exceeds limit; use FileHandle streaming");
}
inline void checkPath(const std::string& path) {
  if (path.empty() || path.find('\0') != std::string::npos) throw std::invalid_argument("Path must be nonempty and contain no NUL bytes");
}
} // namespace rct_io
