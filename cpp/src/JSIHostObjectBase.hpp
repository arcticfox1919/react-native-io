/**
 * @file JSIHostObjectBase.hpp
 * @brief Generic CRTP base class for JSI Host Objects
 *
 * Provides elegant method/property registration via macros.
 * Can be reused by any JSI Host Object implementation.
 *
 * Macros can be disabled by defining JSI_NO_MACROS before including this file.
 *
 * Copyright (c) 2025 arcticfox
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef JSI_HOST_OBJECT_BASE_HPP
#define JSI_HOST_OBJECT_BASE_HPP

#include <jsi/jsi.h>
#include "JSIBuffer.hpp"
#include "IOLimits.hpp"
#include "AsyncPayloadBudget.hpp"
#include "JSIRuntimeState.hpp"
#include <cmath>
#include <atomic>
#include <concepts>
#include <memory>
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <tuple>
#include <variant>
#include <stdexcept>

namespace jsi_utils {

using namespace facebook::jsi;

// ============================================================================
// Async Result Type (Thread-Safe)
// ============================================================================

/**
 * @brief Result type for async operations
 *
 * Async handlers return pure C++ data types, which are converted to JS values
 * on the JS thread. This ensures thread safety since Runtime is not thread-safe.
 */
struct AsyncResult {
    using DataType = std::variant<
        std::monostate,                              // undefined
        bool,                                        // boolean
        double,                                      // number
        std::string,                                 // string
        std::vector<uint8_t>,                        // ArrayBuffer
        std::vector<AsyncResult>,                    // Array
        std::unordered_map<std::string, AsyncResult> // Object
    >;

    DataType data;

    // Constructors for convenience
    AsyncResult() : data(std::monostate{}) {}
    AsyncResult(bool v) : data(v) {}
    AsyncResult(int v) : data(static_cast<double>(v)) {}
    AsyncResult(int64_t v) : data(static_cast<double>(v)) {}
    AsyncResult(uint64_t v) : data(static_cast<double>(v)) {}
    AsyncResult(double v) : data(v) {}
    AsyncResult(const char* v) : data(std::string(v)) {}
    AsyncResult(std::string v) : data(std::move(v)) {}
    AsyncResult(std::vector<uint8_t> v) : data(std::move(v)) {}
    AsyncResult(std::vector<AsyncResult> v) : data(std::move(v)) {}
    AsyncResult(std::unordered_map<std::string, AsyncResult> v) : data(std::move(v)) {}

    /**
     * @brief Convert AsyncResult to JS Value (must be called on JS thread)
     */
    size_t payloadBytes() const {
        return std::visit([](const auto& value) -> size_t {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::vector<uint8_t>>) return value.size();
            else if constexpr (std::is_same_v<T, std::vector<AsyncResult>>) {
                size_t total = 0;
                for (const auto& item : value) {
                    auto bytes = item.payloadBytes();
                    if (bytes > rct_io::AsyncPayloadLease::limit - total) throw std::length_error("Async result exceeds payload budget");
                    total += bytes;
                }
                return total;
            } else if constexpr (std::is_same_v<T, std::unordered_map<std::string, AsyncResult>>) {
                size_t total = 0;
                for (const auto& [key, item] : value) {
                    auto bytes = item.payloadBytes();
                    if (key.size() > rct_io::AsyncPayloadLease::limit - total) throw std::length_error("Async result exceeds payload budget");
                    total += key.size();
                    if (bytes > rct_io::AsyncPayloadLease::limit - total) throw std::length_error("Async result exceeds payload budget");
                    total += bytes;
                }
                return total;
            } else return 0;
        }, data);
    }

    Value toJSValue(Runtime& rt) && {
        return std::visit([&rt](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return Value::undefined();
            } else if constexpr (std::is_same_v<T, bool>) {
                return Value(arg);
            } else if constexpr (std::is_same_v<T, double>) {
                return Value(arg);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return String::createFromUtf8(rt, arg);
            } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
                return toArrayBuffer(rt, std::move(arg));
            } else if constexpr (std::is_same_v<T, std::vector<AsyncResult>>) {
                Array arr(rt, arg.size());
                for (size_t i = 0; i < arg.size(); ++i) {
                    arr.setValueAtIndex(rt, i, std::move(arg[i]).toJSValue(rt));
                }
                return std::move(arr);
            } else if constexpr (std::is_same_v<T, std::unordered_map<std::string, AsyncResult>>) {
                Object obj(rt);
                for (auto& [key, value] : arg) {
                    obj.setProperty(rt, key.c_str(), std::move(value).toJSValue(rt));
                }
                return std::move(obj);
            }
        }, std::move(data));
    }
};

// ============================================================================
// JS Call Invoker Wrapper
// ============================================================================

/**
 * @brief Wrapper interface for scheduling callbacks on the JS thread
 *
 * This wraps the React Native CallInvoker to provide a Runtime& to callbacks,
 * enabling safe JS value creation on the JS thread.
 *
 * Note: Named JSCallInvokerWrapper to avoid conflict with facebook::react::CallInvoker
 */
class JSCallInvokerWrapper {
public:
    virtual ~JSCallInvokerWrapper() = default;

    /**
     * @brief Schedule a callback on the JS thread
     * @param func Callback that receives Runtime& for safe JS value creation
     */
    virtual void invokeAsync(std::function<void(Runtime&)>&& func) = 0;
};

// ============================================================================
// Task Executor Interface
// ============================================================================

/**
 * @brief Interface for executing tasks asynchronously
 *
 * Implementations can use thread pools, single worker threads,
 * dispatch queues, or any other async execution mechanism.
 *
 * @example Thread pool implementation:
 * ```cpp
 * class ThreadPoolExecutor : public TaskExecutor {
 *     BS::thread_pool<> pool_;
 * public:
 *     void execute(std::function<void()>&& task) override {
 *         pool_.detach_task(std::move(task));
 *     }
 * };
 * ```
 */
class TaskExecutor {
public:
    virtual ~TaskExecutor() = default;
    virtual void execute(std::function<void()>&& task) = 0;
};

// ============================================================================
// Promise Callbacks Container
// ============================================================================

/**
 * @brief Container for Promise resolve/reject callbacks
 *
 * Holds both callbacks in a single allocation for async method execution.
 * Must be used on the JS thread only.
 */
// ============================================================================
// Concept: JSIHostObjectDerived
// ============================================================================

/**
 * @brief Concept to enforce derived class requirements
 *
 * Derived classes MUST implement:
 *   - initMethods(): Register sync/async methods
 *
 * Derived classes MAY implement:
 *   - initProperties(): Register properties
 *   - getTaskExecutor(): Return executor for async methods (required if using async)
 */
template <typename T>
concept JSIHostObjectDerived = requires(T t) {
    { t.initMethods() } -> std::same_as<void>;
};

// ============================================================================
// JSI Host Object Base (CRTP)
// ============================================================================

/**
 * @brief CRTP base class for JSI Host Objects
 *
 * Derived class MUST implement:
 *   - initMethods() -> register sync/async methods
 *
 * Derived class MAY implement:
 *   - initProperties() -> register properties
 *   - getTaskExecutor() -> return TaskExecutor* for async methods
 *
 * @tparam Derived The derived class type
 *
 * @note The concept check is deferred to init() call time to work with CRTP
 *
 * @example
 * ```cpp
 * class MyHostObject : public JSIHostObjectBase<MyHostObject> {
 *     friend class JSIHostObjectBase<MyHostObject>;
 *     std::unique_ptr<TaskExecutor> executor_;
 * public:
 *     MyHostObject(std::shared_ptr<CallInvoker> invoker) {
 *         callInvoker_ = invoker;
 *         executor_ = std::make_unique<MyThreadPoolExecutor>();
 *         init();
 *     }
 *     TaskExecutor* getTaskExecutor() { return executor_.get(); }  // Only if using async
 * private:
 *     void initProperties() { ... }  // Optional
 *     void initMethods() { ... }     // Required
 * };
 * ```
 */
template <typename Derived>
class JSIHostObjectBase : public HostObject, public std::enable_shared_from_this<Derived> {
public:
    // ========================================================================
    // Handler Types
    // ========================================================================

    /** @brief Sync handler: runs on JS thread, can use Runtime directly */
    using SyncHandler = std::function<Value(Runtime&, const Value*, size_t)>;

    /**
     * @brief Async handler: runs on worker thread, returns C++ data only
     *
     * Parameters:
     *   - strings: All string arguments (in order)
     *   - numbers: All number arguments (in order)
     *   - bools: All boolean arguments (in order)
     *   - buffers: All ArrayBuffer arguments (as byte vectors)
     *
     * Returns: AsyncResult containing pure C++ data (converted to JS on JS thread)
     * Throws: std::exception on error (will be converted to Promise rejection)
     */
    using AsyncHandler = std::function<AsyncResult(
        const std::vector<std::string>& strings,
        const std::vector<double>& numbers,
        const std::vector<bool>& bools,
        const std::vector<std::vector<uint8_t>>& buffers
    )>;

    using PropertyGetter = std::function<Value(Runtime&)>;
    using PropertySetter = std::function<void(Runtime&, const Value&)>;

    // ========================================================================
    // Registry Entry Types
    // ========================================================================

    struct SyncMethod {
        size_t paramCount;
        SyncHandler handler;
    };

    struct AsyncMethod {
        size_t paramCount;
        AsyncHandler handler;
    };

    struct Property {
        PropertyGetter getter;
        PropertySetter setter;  // nullptr = read-only
    };

protected:
    std::unordered_map<std::string, SyncMethod> syncMethods_;
    std::unordered_map<std::string, AsyncMethod> asyncMethods_;
    std::unordered_map<std::string, Property> properties_;
    std::shared_ptr<JSCallInvokerWrapper> callInvoker_;
    bool hasAsyncMethods_ = false;

    TaskExecutor* taskExecutor() {
        if constexpr (requires(Derived& d) { d.getTaskExecutor(); })
            return static_cast<Derived*>(this)->getTaskExecutor();
        throw std::runtime_error("No task executor");
    }
    const uint64_t hostId_ = nextHostId();
    std::shared_ptr<void> lifetimeToken_ = std::make_shared<int>(0);
    inline static std::atomic<size_t> pendingTasks_{0};
    static constexpr size_t maxPendingTasks_ = 256;

    struct PendingTaskLease {
        ~PendingTaskLease() { --pendingTasks_; }
    };

    static std::shared_ptr<PendingTaskLease> acquirePendingTask() {
        size_t current = pendingTasks_.load();
        do {
            if (current >= maxPendingTasks_)
                throw std::runtime_error("Too many pending I/O operations (limit: 256)");
        } while (!pendingTasks_.compare_exchange_weak(current, current + 1));
        try {
            return std::make_shared<PendingTaskLease>();
        } catch (...) {
            --pendingTasks_;
            throw;
        }
    }

    // ========================================================================
    // Initialization
    // ========================================================================

    /**
     * @brief Initialize the host object
     *
     * Must be called at the END of derived class constructor.
     * Validates async requirements after method registration.
     *
     * @throws std::runtime_error if async methods registered without proper setup
     */
    void init() {
        auto* derived = static_cast<Derived*>(this);

        // Call initProperties if derived class provides it
        if constexpr (requires { derived->initProperties(); }) {
            derived->initProperties();
        }

        // Call initMethods (required by concept)
        derived->initMethods();

        // Validate async requirements after registration
        if (hasAsyncMethods_) {
            if (!callInvoker_) {
                throw std::runtime_error(
                    "JSIHostObjectBase: Async methods registered but callInvoker_ is null. "
                    "Set callInvoker_ before calling init()."
                );
            }

            // Check getTaskExecutor exists and returns non-null
            if constexpr (requires { derived->getTaskExecutor(); }) {
                if (!derived->getTaskExecutor()) {
                    throw std::runtime_error(
                        "JSIHostObjectBase: Async methods registered but getTaskExecutor() returns null."
                    );
                }
            } else {
                throw std::runtime_error(
                    "JSIHostObjectBase: Async methods registered but getTaskExecutor() not implemented."
                );
            }
        }
    }

    // ========================================================================
    // Registration Methods
    // ========================================================================

    void registerSync(const std::string& name, size_t paramCount, SyncHandler handler) {
        syncMethods_[name] = {paramCount, std::move(handler)};
    }

    void registerAsync(const std::string& name, size_t paramCount, AsyncHandler handler) {
        hasAsyncMethods_ = true;
        asyncMethods_[name] = {paramCount, std::move(handler)};
    }

    /** @brief Register a read-only property */
    void registerProperty(const std::string& name, PropertyGetter getter) {
        properties_[name] = {std::move(getter), nullptr};
    }

    /** @brief Register a read-write property */
    void registerProperty(const std::string& name, PropertyGetter getter, PropertySetter setter) {
        properties_[name] = {std::move(getter), std::move(setter)};
    }

    // ========================================================================
    // Helper: Extract Arguments by Type
    // ========================================================================

    /**
     * @brief Extract arguments from JS values into typed vectors
     *
     * Groups arguments by type for easy access in async handlers.
     * Arrays of strings are flattened into the strings vector.
     */
    static auto extractArgs(Runtime& rt, const Value* args, size_t count, rct_io::AsyncPayloadLease& budget)
        -> std::tuple<
            std::vector<std::string>,
            std::vector<double>,
            std::vector<bool>,
            std::vector<std::vector<uint8_t>>
        >
    {
        std::vector<std::string> strings;
        std::vector<double> numbers;
        std::vector<bool> bools;
        std::vector<std::vector<uint8_t>> buffers;

        size_t totalBytes = 0;
        auto addString = [&](std::string text) {
            if (text.size() > rct_io::maxBufferBytes - totalBytes) throw JSError(rt, "I/O arguments exceed buffer limit");
            budget.add(text.size());
            totalBytes += text.size();
            strings.push_back(std::move(text));
        };
        for (size_t i = 0; i < count; ++i) {
            if (args[i].isString()) {
                addString(args[i].asString(rt).utf8(rt));
            } else if (args[i].isNumber()) {
                auto number = args[i].asNumber();
                if (!std::isfinite(number) || std::floor(number) != number ||
                    std::abs(number) > 9007199254740991.0) {
                    throw JSError(rt, "Numeric arguments must be finite safe integers");
                }
                numbers.push_back(number);
            } else if (args[i].isBool()) {
                bools.push_back(args[i].asBool());
            } else if (args[i].isObject()) {
                auto obj = args[i].asObject(rt);
                if (obj.isArrayBuffer(rt)) {
                    auto ab = obj.getArrayBuffer(rt);
                    auto* data = ab.data(rt);
                    auto size = ab.size(rt);
                    if (size > rct_io::maxBufferBytes - totalBytes) throw JSError(rt, "I/O arguments exceed buffer limit");
                    budget.add(size);
                    totalBytes += size;
                    buffers.emplace_back();
                    if (size != 0) buffers.back().assign(data, data + size);
                } else if (obj.isArray(rt)) {
                    // Flatten string arrays into strings vector
                    auto arr = obj.asArray(rt);
                    auto len = arr.size(rt);
                    for (size_t j = 0; j < len; ++j) {
                        auto elem = arr.getValueAtIndex(rt, j);
                        if (!elem.isString()) throw JSError(rt, "Expected an array of strings");
                        addString(elem.asString(rt).utf8(rt));
                    }
                }
            }
        }

        return {std::move(strings), std::move(numbers), std::move(bools), std::move(buffers)};
    }

public:
    JSIHostObjectBase() = default;
    ~JSIHostObjectBase() override = default;

    // ========================================================================
    // HostObject Interface
    // ========================================================================

    Value get(Runtime& rt, const PropNameID& name) override {
        auto propName = name.utf8(rt);

        auto state = runtimeState(rt);
        // Weak JSI methods must be destroyed on the JS thread. Prune expired
        // hosts lazily here instead of posting cleanup work from a destructor
        // while the RN runtime and CallInvoker may already be tearing down.
        if ((++state->methodCacheTouches % 64) == 0 || state->methods.size() > 1024) {
            for (auto it = state->methods.begin(); it != state->methods.end();) {
                if (it->second.lifetime.expired()) it = state->methods.erase(it);
                else ++it;
            }
        }
        auto& hostCache = state->methods[hostId_];
        hostCache.lifetime = lifetimeToken_;
        auto& cache = hostCache.methods;
        if (auto it = cache.find(propName); it != cache.end()) {
            auto cached = it->second.lock(rt);
            if (!cached.isUndefined()) return cached;
        }
        std::weak_ptr<Derived> weakOwner = this->shared_from_this();
        auto validate = [propName](Runtime& runtime, const Value* args, size_t count) {
            if constexpr (requires(Derived& d) { d.validateArguments(runtime, propName, args, count); }) {
                Derived::validateArguments(runtime, propName, args, count);
            }
        };

        if (auto it = syncMethods_.find(propName); it != syncMethods_.end()) {
            auto func = Function::createFromHostFunction(rt, PropNameID::forUtf8(rt, propName),
                it->second.paramCount,
                [weakOwner, handler = it->second.handler, validate, propName](Runtime& runtime, const Value&, const Value* args, size_t count) -> Value {
                    auto owner = weakOwner.lock();
                    if (!owner) throw JSError(runtime, "I/O context has been released");
                    try {
                        if constexpr (requires(Derived& d) { d.validateLifecycle(propName); })
                            owner->validateLifecycle(propName);
                        validate(runtime, args, count);
                        return handler(runtime, args, count);
                    } catch (const std::exception& e) {
                        throw JSError(runtime, e.what());
                    }
                });
            cache.insert_or_assign(propName, WeakObject(rt, func));
            return func;
        }

        if (auto it = asyncMethods_.find(propName); it != asyncMethods_.end()) {
            auto func = Function::createFromHostFunction(rt, PropNameID::forUtf8(rt, propName),
                it->second.paramCount,
                [weakOwner, handler = it->second.handler, validate, propName](Runtime& runtime, const Value&, const Value* args, size_t count) -> Value {
                    try {
                        auto owner = weakOwner.lock();
                        if (!owner) throw JSError(runtime, "I/O context has been released");
                        if constexpr (requires(Derived& d) { d.validateLifecycle(propName); })
                            owner->validateLifecycle(propName);
                        validate(runtime, args, count);
                        // This counter is shared by every HostObject of the same
                        // native type and is released only when the JS callback
                        // runs or is discarded by a torn-down CallInvoker.
                        auto pendingTask = acquirePendingTask();
                        auto inputBudget = std::make_shared<rct_io::AsyncPayloadLease>(rct_io::AsyncPayloadLease::Kind::Input);
                        auto [strings, numbers, bools, buffers] = extractArgs(runtime, args, count, *inputBudget);
                        if constexpr (requires { Derived::appendArgumentMetadata(runtime, propName, args, numbers); })
                            Derived::appendArgumentMetadata(runtime, propName, args, numbers);
                        auto state = runtimeState(runtime);
                        auto& promise = *state->promiseConstructor;
                        auto executor = Function::createFromHostFunction(runtime, PropNameID::forAscii(runtime, "executor"), 2,
                            [owner, handler, state, pendingTask = std::move(pendingTask), inputBudget = std::move(inputBudget), strings = std::move(strings), numbers = std::move(numbers),
                             bools = std::move(bools), buffers = std::move(buffers)]
                            (Runtime& rt, const Value&, const Value* callbacks, size_t) mutable -> Value {
                                auto id = state->nextPromise++;
                                state->pending.emplace(id, std::make_unique<PromiseCallbacks>(rt, callbacks[0], callbacks[1]));
                                std::weak_ptr<RuntimeState> weak = state;
                                try {
                                    owner->taskExecutor()->execute([owner, handler, weak, id, pendingTask = std::move(pendingTask), inputBudget = std::move(inputBudget),
                                        strings = std::move(strings), numbers = std::move(numbers),
                                        bools = std::move(bools), buffers = std::move(buffers)]() mutable {
                                        AsyncResult result;
                                        std::string error;
                                        auto outputBudget = std::make_shared<rct_io::AsyncPayloadLease>(rct_io::AsyncPayloadLease::Kind::Output);
                                        try {
                                            result = handler(strings, numbers, bools, buffers);
                                            outputBudget->add(result.payloadBytes());
                                        } catch (const std::exception& e) { result = AsyncResult{}; error = e.what(); }
                                          catch (...) { result = AsyncResult{}; error = "Unknown native I/O error"; }
                                        strings.clear(); buffers.clear();
                                        inputBudget.reset();
                                        owner->callInvoker_->invokeAsync([owner, weak, id, pendingTask = std::move(pendingTask), outputBudget = std::move(outputBudget), result = std::move(result), error = std::move(error)](Runtime& js) mutable {
                                            auto state = weak.lock();
                                            if (!state) return; // Runtime was destroyed; no stale Runtime/JSI handles.
                                            auto it = state->pending.find(id);
                                            if (it == state->pending.end()) return;
                                            auto callbacks = std::move(it->second);
                                            state->pending.erase(it);
                                            if (!error.empty()) {
                                                callbacks->reject.asObject(js).asFunction(js).call(js, String::createFromUtf8(js, error));
                                                return;
                                            }
                                            try {
                                                auto value = std::move(result).toJSValue(js);
                                                callbacks->resolve.asObject(js).asFunction(js).call(js, std::move(value));
                                            } catch (const std::exception& e) {
                                                callbacks->reject.asObject(js).asFunction(js).call(js, String::createFromUtf8(js, e.what()));
                                            }
                                        });
                                    });
                                } catch (const std::exception& e) {
                                    state->pending.erase(id);
                                    throw JSError(rt, e.what());
                                }
                                return Value::undefined();
                            });
                        return promise.callAsConstructor(runtime, executor);
                    } catch (const JSError&) { throw; }
                      catch (const std::exception& e) { throw JSError(runtime, e.what()); }
                });
            cache.insert_or_assign(propName, WeakObject(rt, func));
            return func;
        }

        // Property lookup
        if (auto it = properties_.find(propName); it != properties_.end()) {
            try {
                return it->second.getter(rt);
            } catch (const std::exception& e) {
                throw JSError(rt, e.what());
            }
        }

        return Value::undefined();
    }

    void set(Runtime& rt, const PropNameID& name, const Value& value) override {
        auto propName = name.utf8(rt);

        if (auto it = properties_.find(propName); it != properties_.end()) {
            if (it->second.setter) {
                try {
                    it->second.setter(rt, value);
                } catch (const std::exception& e) {
                    throw JSError(rt, e.what());
                }
            }
            // Silently ignore writes to read-only properties
        }
    }

    std::vector<PropNameID> getPropertyNames(Runtime& rt) override {
        std::vector<PropNameID> names;
        names.reserve(syncMethods_.size() + asyncMethods_.size() + properties_.size());

        for (const auto& [name, _] : syncMethods_) {
            names.push_back(PropNameID::forUtf8(rt, name));
        }
        for (const auto& [name, _] : asyncMethods_) {
            names.push_back(PropNameID::forUtf8(rt, name));
        }
        for (const auto& [name, _] : properties_) {
            names.push_back(PropNameID::forUtf8(rt, name));
        }

        return names;
    }
};

} // namespace jsi_utils

// ============================================================================
// Optional Macros
// ============================================================================
// Define JSI_NO_MACROS before including this file to disable all macros.
// All macros use JSI_ prefix to avoid collisions.
// Each macro is wrapped in #ifndef to allow user override.

#ifndef JSI_NO_MACROS

// ----------------------------------------------------------------------------
// Argument Extraction Macros (for sync methods)
// Available in body: rt, args, count
// ----------------------------------------------------------------------------

#ifndef JSI_ARG_STR
#define JSI_ARG_STR(idx) args[idx].asString(rt).utf8(rt)
#endif

#ifndef JSI_ARG_NUM
#define JSI_ARG_NUM(idx) args[idx].asNumber()
#endif

#ifndef JSI_ARG_BOOL
#define JSI_ARG_BOOL(idx) args[idx].asBool()
#endif

#ifndef JSI_ARG_BOOL_OPT
#define JSI_ARG_BOOL_OPT(idx, def) (count > idx && !args[idx].isUndefined() ? args[idx].asBool() : def)
#endif

#ifndef JSI_ARG_NUM_OPT
#define JSI_ARG_NUM_OPT(idx, def) (count > idx && !args[idx].isUndefined() ? args[idx].asNumber() : def)
#endif

#ifndef JSI_ARG_BUFFER
#define JSI_ARG_BUFFER(idx) args[idx].asObject(rt).getArrayBuffer(rt)
#endif

// ----------------------------------------------------------------------------
// Async Argument Macros (for async methods)
// Required args:
//   - JSI_S_ARG(idx)   : required string arg
//   - JSI_N_ARG(idx)   : required number arg
//   - JSI_B_ARG(idx)   : required bool arg
//   - JSI_BUF_ARG(idx) : required buffer arg
// Optional args with default:
//   - JSI_S_OPT(idx, def) : optional string arg
//   - JSI_N_OPT(idx, def) : optional number arg
//   - JSI_B_OPT(idx, def) : optional bool arg
// ----------------------------------------------------------------------------

// Required argument macros
#ifndef JSI_S_ARG
#define JSI_S_ARG(idx) jsiStrArgs.at(idx)
#endif

#ifndef JSI_N_ARG
#define JSI_N_ARG(idx) jsiNumArgs.at(idx)
#endif

#ifndef JSI_B_ARG
#define JSI_B_ARG(idx) jsiBoolArgs.at(idx)
#endif

#ifndef JSI_BUF_ARG
#define JSI_BUF_ARG(idx) jsiBufArgs.at(idx)
#endif

// Optional argument macros with default value
#ifndef JSI_S_OPT
#define JSI_S_OPT(idx, def) (jsiStrArgs.size() > (idx) ? jsiStrArgs.at(idx) : (def))
#endif

#ifndef JSI_N_OPT
#define JSI_N_OPT(idx, def) (jsiNumArgs.size() > (idx) ? jsiNumArgs.at(idx) : (def))
#endif

#ifndef JSI_B_OPT
#define JSI_B_OPT(idx, def) (jsiBoolArgs.size() > (idx) ? jsiBoolArgs.at(idx) : (def))
#endif

// ----------------------------------------------------------------------------
// JS Value Creation Macros (for sync methods ONLY - NOT thread safe)
// ----------------------------------------------------------------------------

#ifndef JSI_STRING
#define JSI_STRING(str) String::createFromUtf8(rt, str)
#endif

#ifndef JSI_UNDEFINED
#define JSI_UNDEFINED Value::undefined()
#endif

#ifndef JSI_BOOL
#define JSI_BOOL(b) Value(b)
#endif

#ifndef JSI_NUM
#define JSI_NUM(n) Value(static_cast<double>(n))
#endif

// ----------------------------------------------------------------------------
// Method Registration Macros
// ----------------------------------------------------------------------------

/**
 * @brief Register a synchronous method
 * @param NAME Method name (becomes JS property name)
 * @param PARAM_COUNT Number of parameters
 * @param BODY Lambda body returning Value
 *
 * Available in BODY: rt, args, count
 */
#ifndef JSI_SYNC_METHOD
#define JSI_SYNC_METHOD(NAME, PARAM_COUNT, BODY) \
    registerSync(#NAME, PARAM_COUNT, [this](Runtime& rt, const Value* args, size_t count) -> Value { \
        (void)this; (void)rt; (void)args; (void)count; \
        BODY \
    });
#endif

/**
 * @brief Register an async method (returns Promise)
 * @param NAME Method name
 * @param PARAM_COUNT Number of parameters
 * @param BODY Lambda body returning AsyncResult (pure C++ data)
 *
 * Available in BODY:
 *   - jsiStrArgs (vector of string args)
 *   - jsiNumArgs (vector of number args)
 *   - jsiBoolArgs (vector of bool args)
 *   - jsiBufArgs (vector of ArrayBuffer args as byte vectors)
 *
 * @example
 * JSI_ASYNC_METHOD(readFile, 1, {
 *     std::string content = readFileContent(jsiStrArgs[0]);
 *     return jsi_utils::AsyncResult(content);
 * })
 */
#ifndef JSI_ASYNC_METHOD
#define JSI_ASYNC_METHOD(NAME, PARAM_COUNT, BODY) \
    registerAsync(#NAME, PARAM_COUNT, [this]( \
        const std::vector<std::string>& jsiStrArgs, \
        const std::vector<double>& jsiNumArgs, \
        const std::vector<bool>& jsiBoolArgs, \
        const std::vector<std::vector<uint8_t>>& jsiBufArgs \
    ) -> jsi_utils::AsyncResult { \
        (void)jsiStrArgs; (void)jsiNumArgs; (void)jsiBoolArgs; (void)jsiBufArgs; \
        BODY \
    });
#endif

// ----------------------------------------------------------------------------
// Property Registration Macros
// ----------------------------------------------------------------------------

/**
 * @brief Register a read-only property
 * @param NAME Property name
 * @param BODY Lambda body returning Value
 *
 * Available in BODY: rt
 */
#ifndef JSI_PROPERTY
#define JSI_PROPERTY(NAME, BODY) \
    registerProperty(#NAME, [this](Runtime& rt) -> Value { \
        (void)this; (void)rt; \
        BODY \
    });
#endif

/**
 * @brief Register a read-write property
 * @param NAME Property name
 * @param GETTER_BODY Getter body returning Value
 * @param SETTER_BODY Setter body (receives `value`)
 */
#ifndef JSI_PROPERTY_RW
#define JSI_PROPERTY_RW(NAME, GETTER_BODY, SETTER_BODY) \
    registerProperty(#NAME, \
        [this](Runtime& rt) -> Value { \
            (void)rt; \
            GETTER_BODY \
        }, \
        [this](Runtime& rt, const Value& value) { \
            (void)rt; \
            SETTER_BODY \
        } \
    );
#endif

#endif // JSI_NO_MACROS

#endif // JSI_HOST_OBJECT_BASE_HPP
