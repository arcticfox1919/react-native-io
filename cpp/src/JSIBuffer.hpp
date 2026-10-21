#pragma once

#include <jsi/jsi.h>
#include <cstring>
#include <vector>
#include "JSIRuntimeState.hpp"
#include "IOLimits.hpp"

namespace jsi_utils {
// On runtimes supporting MutableBuffer, move vector ownership into the returned
// ArrayBuffer without another payload copy at this boundary. File/HTTP I/O and
// input snapshots still copy; the JSC fallback below also copies after validation.
class ByteBuffer final : public facebook::jsi::MutableBuffer {
 public:
  explicit ByteBuffer(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}
  explicit ByteBuffer(size_t size) : bytes_(size) {}
  size_t size() const override { return bytes_.size(); }
  uint8_t* data() override { return bytes_.data(); }
 private:
  std::vector<uint8_t> bytes_;
};

inline facebook::jsi::ArrayBuffer toArrayBuffer(
    facebook::jsi::Runtime& rt, std::vector<uint8_t> bytes) {
  rct_io::checkBufferSize(bytes.size());
  auto state = runtimeState(rt);
  auto storage = std::make_shared<ByteBuffer>(std::move(bytes));
  if (state->nativeBuffers) {
    try { return facebook::jsi::ArrayBuffer(rt, storage); }
    catch (const std::logic_error& error) {
      if (std::string(error.what()) != "Not implemented") throw;
      state->nativeBuffers = false; // RN's JSC adapter does not implement MutableBuffer.
    }
  }
  auto value = state->arrayBufferConstructor->callAsConstructor(rt, static_cast<double>(storage->size()));
  if (!value.isObject() || !value.asObject(rt).isArrayBuffer(rt))
    throw facebook::jsi::JSError(rt, "ArrayBuffer constructor returned an invalid buffer");
  auto buffer = value.asObject(rt).getArrayBuffer(rt);
  if (buffer.size(rt) != storage->size()) throw facebook::jsi::JSError(rt, "ArrayBuffer capacity mismatch");
  if (storage->size()) std::memcpy(buffer.data(rt), storage->data(), storage->size());
  return buffer;
}
} // namespace jsi_utils
