#pragma once
#include <jsi/jsi.h>
#include <cstring>
#include <optional>
#include <vector>
#include "IOLimits.hpp"

namespace jsi_utils {
// ASCII content may live in UTF-16 engine storage. Avoid the engine's generic
// UTF-16-to-UTF-8 converter for that case. Non-ASCII keeps engine-defined
// Unicode/replacement semantics. No runtime operations occur inside callbacks.
inline std::optional<std::vector<uint8_t>> tryEncodeAscii(
    facebook::jsi::Runtime& rt, const facebook::jsi::String& str) {
  size_t length = 0;
  bool allAscii = true;
  auto inspect = [&](bool ascii, const void* data, size_t count) {
    if (!allAscii) return;
    if (count > rct_io::maxBufferBytes - length)
      throw std::length_error("I/O buffer exceeds limit; use FileHandle streaming");
    if (!ascii) {
      const auto* units = static_cast<const char16_t*>(data);
      char16_t combined = 0;
      for (size_t i = 0; i < count; ++i) combined |= units[i];
      if (combined > 127) { allAscii = false; return; }
    }
    length += count;
  };
  str.getStringData(rt, inspect);
  if (!allAscii) return std::nullopt;

  std::vector<uint8_t> bytes(length);
  size_t offset = 0;
  auto copy = [&](bool ascii, const void* data, size_t count) {
    if (count > bytes.size() - offset) throw std::runtime_error("String length changed during encoding");
    if (count != 0) {
      if (ascii) std::memcpy(bytes.data() + offset, data, count);
      else {
        const auto* units = static_cast<const char16_t*>(data);
        for (size_t i = 0; i < count; ++i) bytes[offset + i] = static_cast<uint8_t>(units[i]);
      }
    }
    offset += count;
  };
  str.getStringData(rt, copy);
  if (offset != bytes.size()) throw std::runtime_error("String length changed during encoding");
  return bytes;
}
} // namespace jsi_utils
