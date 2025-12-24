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
    Value toJSValue(Runtime& rt) const {
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
                // Create ArrayBuffer and copy data
                auto arrayBufferCtor = rt.global().getPropertyAsFunction(rt, "ArrayBuffer");
                auto arrayBufferObj = arrayBufferCtor.callAsConstructor(rt, static_cast<int>(arg.size())).asObject(rt);
                auto arrayBuffer = arrayBufferObj.getArrayBuffer(rt);
                memcpy(arrayBuffer.data(rt), arg.data(), arg.size());
                return std::move(arrayBufferObj);
            } else if constexpr (std::is_same_v<T, std::vector<AsyncResult>>) {
                Array arr(rt, arg.size());
                for (size_t i = 0; i < arg.size(); ++i) {
                    arr.setValueAtIndex(rt, i, arg[i].toJSValue(rt));
                }
                return std::move(arr);
            } else if constexpr (std::is_same_v<T, std::unordered_map<std::string, AsyncResult>>) {
                Object obj(rt);
                for (const auto& [key, value] : arg) {
                    obj.setProperty(rt, key.c_str(), value.toJSValue(rt));
                }
                return std::move(obj);
            }
        }, data);
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
struct PromiseCallbacks {
    Value resolve;
    Value reject;
    PromiseCallbacks(Runtime& rt, const Value& res, const Value& rej)
        : resolve(rt, res), reject(rt, rej) {}
};

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
class JSIHostObjectBase : public HostObject {
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

    // Cached JS objects (created lazily, invalidated on runtime change)
    mutable std::unordered_map<std::string, std::shared_ptr<Object>> cachedFunctions_;
    mutable std::shared_ptr<Object> cachedPromiseCtor_;
    mutable Runtime* cachedRuntime_ = nullptr;

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
    static auto extractArgs(Runtime& rt, const Value* args, size_t count)
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

        for (size_t i = 0; i < count; ++i) {
            if (args[i].isString()) {
                strings.push_back(args[i].asString(rt).utf8(rt));
            } else if (args[i].isNumber()) {
                numbers.push_back(args[i].asNumber());
            } else if (args[i].isBool()) {
                bools.push_back(args[i].asBool());
            } else if (args[i].isObject()) {
                auto obj = args[i].asObject(rt);
                if (obj.isArrayBuffer(rt)) {
                    auto ab = obj.getArrayBuffer(rt);
                    auto* data = ab.data(rt);
                    auto size = ab.size(rt);
                    buffers.emplace_back(data, data + size);
                } else if (obj.isArray(rt)) {
                    // Flatten string arrays into strings vector
                    auto arr = obj.asArray(rt);
                    auto len = arr.size(rt);
                    for (size_t j = 0; j < len; ++j) {
                        auto elem = arr.getValueAtIndex(rt, j);
                        if (elem.isString()) {
                            strings.push_back(elem.asString(rt).utf8(rt));
                        }
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

        // Invalidate cache if runtime changed
        if (cachedRuntime_ != &rt) {
            cachedFunctions_.clear();
            cachedPromiseCtor_.reset();
            cachedRuntime_ = &rt;
        }

        // Check if we have a cached function for this method
        if (auto cacheIt = cachedFunctions_.find(propName); cacheIt != cachedFunctions_.end()) {
            return Value(rt, *cacheIt->second);
        }

        // Sync method lookup
        if (auto it = syncMethods_.find(propName); it != syncMethods_.end()) {
            const auto& method = it->second;
            auto func = Function::createFromHostFunction(
                rt,
                PropNameID::forUtf8(rt, propName),
                method.paramCount,
                [handler = method.handler](Runtime& runtime, const Value&, const Value* args, size_t count) -> Value {
                    try {
                        return handler(runtime, args, count);
                    } catch (const std::exception& e) {
                        throw JSError(runtime, e.what());
                    }
                }
            );
            // Cache the function
            auto cached = std::make_shared<Object>(std::move(func));
            cachedFunctions_[propName] = cached;
            return Value(rt, *cached);
        }

        // Async method lookup
        if (auto it = asyncMethods_.find(propName); it != asyncMethods_.end()) {
            const auto& method = it->second;
            auto invoker = callInvoker_;

            // Get task executor from derived class (required for async methods)
            TaskExecutor* executor = nullptr;
            if constexpr (requires(Derived& d) { d.getTaskExecutor(); }) {
                executor = static_cast<Derived*>(this)->getTaskExecutor();
            }

            auto func = Function::createFromHostFunction(
                rt,
                PropNameID::forUtf8(rt, propName),
                method.paramCount,
                [handler = method.handler, invoker, executor, this](
                    Runtime& runtime, const Value&, const Value* args, size_t count
                ) -> Value {
                    // Extract args on JS thread (safe)
                    auto [strings, numbers, bools, buffers] = extractArgs(runtime, args, count);

                    // Get cached Promise constructor
                    if (!cachedPromiseCtor_) {
                        cachedPromiseCtor_ = std::make_shared<Object>(
                            runtime.global().getPropertyAsFunction(runtime, "Promise")
                        );
                    }

                    auto promiseExecutor = Function::createFromHostFunction(
                        runtime,
                        PropNameID::forUtf8(runtime, "executor"),
                        2,
                        [handler, invoker, executor,
                         strings = std::move(strings),
                         numbers = std::move(numbers),
                         bools = std::move(bools),
                         buffers = std::move(buffers)](
                            Runtime& rt, const Value&, const Value* promiseArgs, size_t
                        ) mutable -> Value {
                            // Use single allocation for both callbacks
                            auto callbacks = std::make_shared<PromiseCallbacks>(rt, promiseArgs[0], promiseArgs[1]);

                            // Execute handler on worker thread
                            executor->execute([handler, invoker, callbacks,
                                               strings = std::move(strings),
                                               numbers = std::move(numbers),
                                               bools = std::move(bools),
                                               buffers = std::move(buffers)]() mutable {
                                try {
                                    // Run handler on worker thread (pure C++ computation, no Runtime)
                                    AsyncResult result = handler(strings, numbers, bools, buffers);

                                    // Return to JS thread to create JS values and resolve
                                    invoker->invokeAsync([callbacks, result = std::move(result)](Runtime& rt) mutable {
                                        Value jsValue = result.toJSValue(rt);
                                        callbacks->resolve.asObject(rt).asFunction(rt).call(rt, std::move(jsValue));
                                    });
                                } catch (const std::exception& e) {
                                    std::string errorMsg = e.what();
                                    // Return to JS thread to reject
                                    invoker->invokeAsync([callbacks, errorMsg = std::move(errorMsg)](Runtime& rt) {
                                        callbacks->reject.asObject(rt).asFunction(rt).call(
                                            rt, String::createFromUtf8(rt, errorMsg)
                                        );
                                    });
                                }
                            });

                            return Value::undefined();
                        }
                    );

                    return cachedPromiseCtor_->asFunction(runtime).callAsConstructor(runtime, promiseExecutor);
                }
            );
            // Cache the function
            auto cached = std::make_shared<Object>(std::move(func));
            cachedFunctions_[propName] = cached;
            return Value(rt, *cached);
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
#define JSI_S_ARG(idx) jsiStrArgs[idx]
#endif

#ifndef JSI_N_ARG
#define JSI_N_ARG(idx) jsiNumArgs[idx]
#endif

#ifndef JSI_B_ARG
#define JSI_B_ARG(idx) jsiBoolArgs[idx]
#endif

#ifndef JSI_BUF_ARG
#define JSI_BUF_ARG(idx) jsiBufArgs[idx]
#endif

// Optional argument macros with default value
#ifndef JSI_S_OPT
#define JSI_S_OPT(idx, def) (jsiStrArgs.size() > (idx) ? jsiStrArgs[idx] : (def))
#endif

#ifndef JSI_N_OPT
#define JSI_N_OPT(idx, def) (jsiNumArgs.size() > (idx) ? jsiNumArgs[idx] : (def))
#endif

#ifndef JSI_B_OPT
#define JSI_B_OPT(idx, def) (jsiBoolArgs.size() > (idx) ? jsiBoolArgs[idx] : (def))
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
