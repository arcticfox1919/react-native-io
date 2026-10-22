#pragma once
#include <jsi/jsi.h>
#include <unordered_map>
#include <cstdint>
#include <atomic>
#include <memory>

namespace jsi_utils {
using namespace facebook::jsi;

struct PromiseCallbacks {
  Value resolve;
  Value reject;
  PromiseCallbacks(Runtime& rt, const Value& res, const Value& rej)
      : resolve(rt, res), reject(rt, rej) {}
};
struct HostMethodCache {
  std::weak_ptr<void> lifetime;
  std::unordered_map<std::string, WeakObject> methods;
};

// Only the runtime owns JS values. Workers hold weak references to this state
// and pure C++ results, never JSI handles or an unguarded Runtime pointer.
struct RuntimeState {
  uint64_t nextPromise = 1;
  std::unique_ptr<Function> arrayBufferConstructor;
  std::unique_ptr<Function> promiseConstructor;
  std::unique_ptr<Function> typedArrayBufferGetter;
  std::unique_ptr<Function> typedArrayOffsetGetter;
  std::unique_ptr<Function> typedArrayLengthGetter;
  std::unique_ptr<Function> typedArrayTagGetter;
  Value uint8ArrayTag;
  bool nativeBuffers = true;
  bool nativeStringData = false;
  uint64_t methodCacheTouches = 0;
  std::unordered_map<uint64_t, std::unique_ptr<PromiseCallbacks>> pending;
  std::unordered_map<uint64_t, HostMethodCache> methods;
};
class RuntimeStateHolder final : public HostObject {
 public:
  std::shared_ptr<RuntimeState> state = std::make_shared<RuntimeState>();
};
inline std::shared_ptr<RuntimeState> runtimeState(Runtime& rt) {
  constexpr auto key = "__react_native_io_runtime_state__";
  auto value = rt.global().getProperty(rt, key);
  if (value.isObject() && value.asObject(rt).isHostObject<RuntimeStateHolder>(rt)) {
    return value.asObject(rt).getHostObject<RuntimeStateHolder>(rt)->state;
  }
  auto holder = std::make_shared<RuntimeStateHolder>();
  holder->state->promiseConstructor = std::make_unique<Function>(rt.global().getPropertyAsFunction(rt, "Promise"));
  holder->state->arrayBufferConstructor = std::make_unique<Function>(rt.global().getPropertyAsFunction(rt, "ArrayBuffer"));
  // The default JSI implementation first converts through UTF-8/UTF-16.
  // Enable direct string access only when the runtime exposes native ASCII.
  auto probe = String::createFromAscii(rt, "a");
  auto inspect = [&](bool ascii, const void*, size_t) { holder->state->nativeStringData = ascii; };
  probe.getStringData(rt, inspect);
  // Capture intrinsic accessors once. Reading properties on the input view
  // would allow overridden getters to supply forged ranges or run user code.
  auto object = rt.global().getPropertyAsObject(rt, "Object");
  auto descriptor = object.getPropertyAsFunction(rt, "getOwnPropertyDescriptor");
  auto prototype = object.getPropertyAsFunction(rt, "getPrototypeOf").call(
      rt, rt.global().getPropertyAsFunction(rt, "Uint8Array").getPropertyAsObject(rt, "prototype")).asObject(rt);
  auto getter = [&](const Value& key) {
    return std::make_unique<Function>(descriptor.call(rt, prototype, key)
        .asObject(rt).getPropertyAsFunction(rt, "get"));
  };
  holder->state->typedArrayBufferGetter = getter(String::createFromAscii(rt, "buffer"));
  holder->state->typedArrayOffsetGetter = getter(String::createFromAscii(rt, "byteOffset"));
  holder->state->typedArrayLengthGetter = getter(String::createFromAscii(rt, "byteLength"));
  holder->state->typedArrayTagGetter = getter(
      rt.global().getPropertyAsObject(rt, "Symbol").getProperty(rt, "toStringTag"));
  holder->state->uint8ArrayTag = String::createFromAscii(rt, "Uint8Array");
  rt.global().setProperty(rt, key, Object::createFromHostObject(rt, holder));
  return holder->state;
}
inline uint64_t nextHostId() {
  static std::atomic<uint64_t> id{0};
  return ++id;
}
} // namespace jsi_utils
