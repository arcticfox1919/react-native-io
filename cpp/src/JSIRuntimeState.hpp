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
  bool nativeBuffers = true;
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
  rt.global().setProperty(rt, key, Object::createFromHostObject(rt, holder));
  return holder->state;
}
inline uint64_t nextHostId() {
  static std::atomic<uint64_t> id{0};
  return ++id;
}
} // namespace jsi_utils
