/**
 * @file IORequestHostObject.hpp
 * @brief JSI Host Object binding for HTTP requests
 *
 * Provides HTTP request operations to JavaScript via JSI.
 *
 * Copyright (c) 2025 arcticfox
 */

#ifndef IO_REQUEST_HOST_OBJECT_HPP
#define IO_REQUEST_HOST_OBJECT_HPP

#include "BS_thread_pool.hpp"
#include "JSIHostObjectBase.hpp"
#include "../network/IOHttpClient.hpp"
#include <ReactCommon/CallInvoker.h>

#ifdef __ANDROID__
#include "../network/IOHttpClientAndroid.hpp"
#endif

namespace rct_io {

using namespace facebook::jsi;
using namespace jsi_utils;
using namespace rct_io::network;

// Type aliases for async result maps
using AsyncResultMap = std::unordered_map<std::string, AsyncResult>;

// ============================================================================
// Thread Pool Task Executor (reuse from FSHostObject pattern)
// ============================================================================

class RequestThreadPoolExecutor : public TaskExecutor {
private:
    std::shared_ptr<BS::thread_pool<>> pool_;

public:
    explicit RequestThreadPoolExecutor(std::shared_ptr<BS::thread_pool<>> pool)
        : pool_(std::move(pool)) {}

    void execute(std::function<void()>&& task) override {
        pool_->detach_task(std::move(task));
    }
};

// ============================================================================
// CallInvoker Adapter
// ============================================================================

class RequestCallInvokerAdapter : public JSCallInvokerWrapper {
private:
    std::shared_ptr<facebook::react::CallInvoker> invoker_;
    Runtime* runtime_;

public:
    RequestCallInvokerAdapter(
        std::shared_ptr<facebook::react::CallInvoker> invoker,
        Runtime& runtime
    ) : invoker_(std::move(invoker)), runtime_(&runtime) {}

    void invokeAsync(std::function<void(Runtime&)>&& func) override {
        auto rt = runtime_;
        invoker_->invokeAsync([rt, func = std::move(func)]() mutable {
            func(*rt);
        });
    }
};

// ============================================================================
// IORequestHostObject Implementation
// ============================================================================

class IORequestHostObject : public JSIHostObjectBase<IORequestHostObject> {
    friend class JSIHostObjectBase<IORequestHostObject>;

private:
    std::shared_ptr<IOHttpClient> client_;
    std::shared_ptr<JSCallInvokerWrapper> invoker_;
    std::unique_ptr<TaskExecutor> executor_;

public:
    /**
     * @brief Construct IORequestHostObject
     * @param runtime JSI Runtime reference
     * @param threadPool Thread pool for async operations
     * @param callInvoker React Native's CallInvoker for JS thread callbacks
     */
    IORequestHostObject(
        Runtime& runtime,
        std::shared_ptr<BS::thread_pool<>> threadPool,
        std::shared_ptr<facebook::react::CallInvoker> callInvoker
    ) : client_(IOHttpClient::create())
      , invoker_(std::make_shared<RequestCallInvokerAdapter>(std::move(callInvoker), runtime))
      , executor_(std::make_unique<RequestThreadPoolExecutor>(std::move(threadPool)))
    {
        callInvoker_ = invoker_;
        init();
    }

    // Required for async methods
    TaskExecutor* getTaskExecutor() { return executor_.get(); }

private:
    // ========================================================================
    // Helper: Convert HttpResponse to AsyncResult
    // ========================================================================
    static AsyncResult responseToAsyncResult(const HttpResponse& response) {
        AsyncResultMap obj;
        obj.emplace("success", response.success);
        obj.emplace("statusCode", static_cast<double>(response.statusCode));
        obj.emplace("statusMessage", response.statusMessage);
        obj.emplace("url", response.url);
        obj.emplace("errorMessage", response.errorMessage);
        obj.emplace("body", response.body);  // vector<uint8_t> -> ArrayBuffer

        // Headers as parallel arrays
        std::vector<AsyncResult> headerKeys;
        std::vector<AsyncResult> headerValues;
        headerKeys.reserve(response.headers.size());
        headerValues.reserve(response.headers.size());
        for (const auto& [key, value] : response.headers) {
            headerKeys.emplace_back(key);
            headerValues.emplace_back(value);
        }
        obj.emplace("headerKeys", std::move(headerKeys));
        obj.emplace("headerValues", std::move(headerValues));

        return AsyncResult(std::move(obj));
    }

    // ========================================================================
    // Helper: Convert DownloadResult to AsyncResult
    // ========================================================================
    static AsyncResult downloadResultToAsyncResult(const DownloadResult& result) {
        AsyncResultMap obj;
        obj.emplace("success", result.success);
        obj.emplace("statusCode", static_cast<double>(result.statusCode));
        obj.emplace("filePath", result.filePath);
        obj.emplace("fileSize", static_cast<double>(result.fileSize));
        obj.emplace("errorMessage", result.errorMessage);
        return AsyncResult(std::move(obj));
    }

    // ========================================================================
    // Helper: Convert UploadResult to AsyncResult
    // ========================================================================
    static AsyncResult uploadResultToAsyncResult(const UploadResult& result) {
        AsyncResultMap obj;
        obj.emplace("success", result.success);
        obj.emplace("statusCode", static_cast<double>(result.statusCode));
        obj.emplace("responseBody", result.responseBody);  // vector<uint8_t> -> ArrayBuffer
        obj.emplace("errorMessage", result.errorMessage);
        return AsyncResult(std::move(obj));
    }

    // ========================================================================
    // Property Registration
    // ========================================================================
    void initProperties() {
        JSI_PROPERTY(version, {
            return JSI_STRING("1.0.0");
        })
    }

    // ========================================================================
    // Method Registration
    // ========================================================================
    void initMethods() {
        // ====================================================================
        // Async HTTP Request
        // ====================================================================

        // request(url, method, headers[], body, timeout, followRedirects) -> Promise
        // JSI_ASYNC_METHOD uses jsiStrArgs, jsiNumArgs, jsiBoolArgs, jsiBufArgs
        // Args order by type extraction:
        //   jsiStrArgs: [url, method, headerKey1, headerVal1, ..., bodyStr?]
        //   jsiNumArgs: [timeout]
        //   jsiBoolArgs: [followRedirects]
        //   jsiBufArgs: [body?] if ArrayBuffer
        JSI_ASYNC_METHOD(request, 6, {
            HttpRequestConfig config;

            // url and method are first two string args
            config.url = JSI_S_ARG(0);
            config.method = stringToHttpMethod(JSI_S_ARG(1));
            config.timeoutMs = static_cast<int32_t>(JSI_N_ARG(0));
            config.followRedirects = JSI_B_ARG(0);

            // Body from buffer args if present
            if (!jsiBufArgs.empty()) {
                config.body = jsiBufArgs[0];
            }

            // Parse headers: all strings from index 2 are header key-value pairs
            // If body was string (not buffer), it would be at end - but we use buffer
            size_t numHeaderStrings = jsiStrArgs.size() - 2;

            // If odd count and no buffer, last string is body
            if (numHeaderStrings % 2 == 1 && jsiBufArgs.empty()) {
                auto bodyStr = jsiStrArgs.back();
                config.body = std::vector<uint8_t>(bodyStr.begin(), bodyStr.end());
                numHeaderStrings--;
            }

            for (size_t i = 0; i < numHeaderStrings; i += 2) {
                config.headers[jsiStrArgs[2 + i]] = jsiStrArgs[2 + i + 1];
            }

            auto response = client_->request(config);
            return responseToAsyncResult(response);
        })

        // ====================================================================
        // Async Download
        // ====================================================================

        // download(url, destinationPath, headers[], timeout, resumable) -> Promise
        JSI_ASYNC_METHOD(download, 5, {
            DownloadConfig config;
            config.url = JSI_S_ARG(0);
            config.destinationPath = JSI_S_ARG(1);
            config.timeoutMs = static_cast<int32_t>(JSI_N_ARG(0));
            config.resumable = JSI_B_ARG(0);

            // Headers are from index 2 onward in string args
            for (size_t i = 2; i + 1 < jsiStrArgs.size(); i += 2) {
                config.headers[jsiStrArgs[i]] = jsiStrArgs[i + 1];
            }

            auto result = client_->download(config);
            return downloadResultToAsyncResult(result);
        })

        // ====================================================================
        // Async Upload
        // ====================================================================

        // upload(url, filePath, fieldName, fileName, mimeType, headers[], formKeys[], formValues[], timeout) -> Promise
        // jsiStrArgs: [url, filePath, fieldName, fileName, mimeType, headers..., formKeys..., formValues...]
        // jsiNumArgs: [timeout, headerCount, formFieldCount]
        JSI_ASYNC_METHOD(upload, 9, {
            UploadConfig config;
            config.url = JSI_S_ARG(0);
            config.filePath = JSI_S_ARG(1);
            config.fieldName = JSI_S_ARG(2);
            config.fileName = JSI_S_ARG(3);
            config.mimeType = JSI_S_ARG(4);
            config.timeoutMs = static_cast<int32_t>(JSI_N_ARG(0));

            // Parse headerCount and formFieldCount from numbers if available
            size_t headerCount = jsiNumArgs.size() > 1 ? static_cast<size_t>(jsiNumArgs[1]) : 0;
            size_t formFieldCount = jsiNumArgs.size() > 2 ? static_cast<size_t>(jsiNumArgs[2]) : 0;

            // Headers start at index 5
            size_t headerStartIdx = 5;
            for (size_t i = 0; i < headerCount * 2 && (headerStartIdx + i + 1) < jsiStrArgs.size(); i += 2) {
                config.headers[jsiStrArgs[headerStartIdx + i]] = jsiStrArgs[headerStartIdx + i + 1];
            }

            // Form fields start after headers
            size_t formStartIdx = headerStartIdx + headerCount * 2;
            for (size_t i = 0; i < formFieldCount && (formStartIdx + i) < jsiStrArgs.size(); ++i) {
                // formKeys and formValues are separate arrays, so we need different handling
                // Let's assume formKeys come first, then formValues
                size_t formKeyIdx = formStartIdx + i;
                size_t formValIdx = formStartIdx + formFieldCount + i;
                if (formValIdx < jsiStrArgs.size()) {
                    config.formFields[jsiStrArgs[formKeyIdx]] = jsiStrArgs[formValIdx];
                }
            }

            auto result = client_->upload(config);
            return uploadResultToAsyncResult(result);
        })
    }
};

} // namespace rct_io

#endif // IO_REQUEST_HOST_OBJECT_HPP
