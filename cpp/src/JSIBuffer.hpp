#pragma once

#include <jsi/jsi.h>
#include <cstring>
#include <vector>
#include <string>
#include <concepts>
#include <span>
#include <cmath>
#include "JSIRuntimeState.hpp"
#include "IOLimits.hpp"

namespace jsi_utils {
// The JSI owner remains on the JS thread. Only its selected range may be
// borrowed synchronously; async callers must copy before queuing work.
struct BorrowedBytes {
  facebook::jsi::ArrayBuffer owner;
  size_t offset;
  size_t length;

  std::span<const uint8_t> view(facebook::jsi::Runtime& rt) const {
    if (length == 0) return {};
    const auto* data = owner.data(rt);
    if (!data) throw facebook::jsi::JSError(rt, "Binary buffer is unavailable");
    return {data + offset, length};
  }
};

inline BorrowedBytes borrowBytes(facebook::jsi::Runtime& rt,
                                const facebook::jsi::Value& value) {
  using namespace facebook::jsi;
  if (!value.isObject()) throw JSError(rt, "Expected ArrayBuffer or Uint8Array");
  auto object = value.asObject(rt);
  if (object.isArrayBuffer(rt)) {
    auto buffer = object.getArrayBuffer(rt);
    auto size = buffer.size(rt);
    rct_io::checkBufferSize(size);
    return {std::move(buffer), 0, size};
  }
  auto state = runtimeState(rt);
  auto tag = state->typedArrayTagGetter->callWithThis(rt, object);
  if (!Value::strictEquals(rt, tag, state->uint8ArrayTag))
    throw JSError(rt, "Expected ArrayBuffer or Uint8Array");
  auto backing = state->typedArrayBufferGetter->callWithThis(rt, object);
  if (!backing.isObject())
    throw JSError(rt, "Uint8Array must be backed by ArrayBuffer; SharedArrayBuffer is unsupported");
  auto backingObject = std::move(backing).asObject(rt);
  if (!backingObject.isArrayBuffer(rt))
    throw JSError(rt, "Uint8Array must be backed by ArrayBuffer; SharedArrayBuffer is unsupported");
  auto buffer = std::move(backingObject).getArrayBuffer(rt);
  const auto capacity = buffer.size(rt);
  auto rangeNumber = [&](const Function& getter) {
    auto result = getter.callWithThis(rt, object);
    if (!result.isNumber()) throw JSError(rt, "Invalid binary view range");
    auto number = result.asNumber();
    if (!std::isfinite(number) || number < 0 || std::floor(number) != number ||
        number > 9007199254740991.0 || number > static_cast<double>(capacity))
      throw JSError(rt, "Invalid binary view range");
    return static_cast<size_t>(number);
  };
  auto offset = rangeNumber(*state->typedArrayOffsetGetter);
  auto length = rangeNumber(*state->typedArrayLengthGetter);
  if (length > capacity - offset) throw JSError(rt, "Binary view exceeds buffer capacity");
  rct_io::checkBufferSize(length);
  return {std::move(buffer), offset, length};
}

// Own the producer's storage directly: converting an encoded string to a vector
// would add a full payload copy. Short strings stay inside this heap-owned object.
// JSC's fallback below still copies into an engine-allocated ArrayBuffer.
template <typename Storage>
  requires (std::same_as<Storage, std::vector<uint8_t>> ||
            std::same_as<Storage, std::string>)
class ByteBuffer final : public facebook::jsi::MutableBuffer {
 public:
  explicit ByteBuffer(Storage bytes) : bytes_(std::move(bytes)) {}
  size_t size() const override { return bytes_.size(); }
  uint8_t* data() override { return reinterpret_cast<uint8_t*>(bytes_.data()); }
 private:
  Storage bytes_;
};

template <typename Storage>
inline facebook::jsi::ArrayBuffer toArrayBuffer(
    facebook::jsi::Runtime& rt, Storage bytes) {
  rct_io::checkBufferSize(bytes.size());
  auto state = runtimeState(rt);
  auto storage = std::make_shared<ByteBuffer<Storage>>(std::move(bytes));
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
