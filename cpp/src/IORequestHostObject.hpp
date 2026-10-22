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
#include "ThreadPoolReaper.hpp"
#include "JSIHostObjectBase.hpp"
#include "JSIArguments.hpp"
#include "../network/IOHttpClient.hpp"
#include <ReactCommon/CallInvoker.h>
#include <algorithm>
#include <filesystem>
#include <string_view>

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
        : pool_(std::move(pool)) { (void)ThreadPoolReaper::instance(); }

    ~RequestThreadPoolExecutor() override { ThreadPoolReaper::instance().retire(std::move(pool_)); }

    void execute(std::function<void()>&& task) override {
        // JSIHostObjectBase owns the global pending-operation lease until JS
        // consumes (or the runtime drops) the completion callback.
        pool_->detach_task(std::move(task));
    }
};

// ============================================================================
// CallInvoker Adapter
// ============================================================================

class RequestCallInvokerAdapter : public JSCallInvokerWrapper {
private:
    std::shared_ptr<facebook::react::CallInvoker> invoker_;

public:
    RequestCallInvokerAdapter(
        std::shared_ptr<facebook::react::CallInvoker> invoker,
        Runtime& /*runtime*/
    ) : invoker_(std::move(invoker)) {}

    void invokeAsync(std::function<void(Runtime&)>&& func) override {
        invoker_->invokeAsync(std::move(func));
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

    static void validateArguments(Runtime& rt, const std::string& name, const Value* args, size_t count) {
        const auto signature = name == "request" ? "SSVDTB" : name == "download" ? "PPVTB" : "PPSSSVVVT";
        validateSignature(rt, name, args, count, signature);
        if ((name == "request" && args[2].asObject(rt).asArray(rt).size(rt) % 2 != 0) ||
            (name == "download" && args[2].asObject(rt).asArray(rt).size(rt) % 2 != 0) ||
            (name == "upload" && (args[5].asObject(rt).asArray(rt).size(rt) % 2 != 0 ||
              args[6].asObject(rt).asArray(rt).size(rt) != args[7].asObject(rt).asArray(rt).size(rt))))
            throw JSError(rt, "Invalid header/form key-value arrays");
    }
    static void validateHeaders(const HttpHeaders& headers) {
        constexpr std::string_view token = "!#$%&'*+-.^_`|~";
        for (const auto& [key, value] : headers) {
            if (!std::all_of(key.begin(), key.end(), [&](unsigned char c) {
                  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || token.find(static_cast<char>(c)) != std::string_view::npos;
                }) || !std::all_of(value.begin(), value.end(), [](unsigned char c) { return c == '\t' || (c >= 32 && c != 127); }))
                throw std::invalid_argument("Invalid HTTP header characters");
            if (key.empty() || key.find_first_of("\r\n") != std::string::npos || value.find_first_of("\r\n") != std::string::npos ||
                key.find('\0') != std::string::npos || value.find('\0') != std::string::npos)
                throw std::invalid_argument("Invalid HTTP header");
        }
    }
    static void validateMultipart(const std::string& text) {
        if (text.find_first_of("\r\n\"\\") != std::string::npos || text.find('\0') != std::string::npos)
            throw std::invalid_argument("Invalid multipart field/file name or MIME type");
    }
    static void appendArgumentMetadata(Runtime& rt, const std::string& name, const Value* args, std::vector<double>& numbers) {
        if (name == "upload") {
            numbers.push_back(args[5].asObject(rt).asArray(rt).size(rt) / 2);
            numbers.push_back(args[6].asObject(rt).asArray(rt).size(rt));
        }
    }

    // Required for async methods
    TaskExecutor* getTaskExecutor() { return executor_.get(); }

private:
    // ========================================================================
    // Helper: Convert HttpResponse to AsyncResult
    // ========================================================================
    static AsyncResult responseToAsyncResult(HttpResponse response) {
        AsyncResultMap obj;
        obj.emplace("success", response.success);
        obj.emplace("statusCode", static_cast<double>(response.statusCode));
        obj.emplace("statusMessage", std::move(response.statusMessage));
        obj.emplace("url", std::move(response.url));
        obj.emplace("errorMessage", std::move(response.errorMessage));
        obj.emplace("body", std::move(response.body));  // vector<uint8_t> -> ArrayBuffer

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
    static AsyncResult downloadResultToAsyncResult(DownloadResult result) {
        AsyncResultMap obj;
        obj.emplace("success", result.success);
        obj.emplace("statusCode", static_cast<double>(result.statusCode));
        obj.emplace("filePath", std::move(result.filePath));
        obj.emplace("fileSize", static_cast<double>(result.fileSize));
        obj.emplace("errorMessage", std::move(result.errorMessage));
        return AsyncResult(std::move(obj));
    }

    // ========================================================================
    // Helper: Convert UploadResult to AsyncResult
    // ========================================================================
    static AsyncResult uploadResultToAsyncResult(UploadResult result) {
        AsyncResultMap obj;
        obj.emplace("success", result.success);
        obj.emplace("statusCode", static_cast<double>(result.statusCode));
        obj.emplace("responseBody", std::move(result.responseBody));  // vector<uint8_t> -> ArrayBuffer
        obj.emplace("errorMessage", std::move(result.errorMessage));
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
            auto method = JSI_S_ARG(1);
            if (method != "GET" && method != "POST" && method != "PUT" && method != "DELETE" && method != "PATCH" && method != "HEAD" && method != "OPTIONS")
                throw std::invalid_argument("Unsupported HTTP method");
            config.method = stringToHttpMethod(method);
            config.timeoutMs = static_cast<int32_t>(JSI_N_ARG(0));
            config.followRedirects = JSI_B_ARG(0);

            // Body from buffer args if present
            if (!jsiBufArgs.empty()) {
                // The JS-thread snapshot is owned by this task and used once.
                config.body = std::move(jsiBufArgs[0]);
            }

            // Header pairs follow URL/method; an optional string body comes last.
            size_t numHeaderStrings = jsiStrArgs.size() - 2;

            // If odd count and no buffer, last string is body
            if (numHeaderStrings % 2 == 1 && jsiBufArgs.empty()) {
                config.bodyString = std::move(jsiStrArgs.back());
                numHeaderStrings--;
            }

            for (size_t i = 0; i < numHeaderStrings; i += 2) {
                config.headers[jsiStrArgs[2 + i]] = jsiStrArgs[2 + i + 1];
            }

            validateHeaders(config.headers);
            auto response = client_->request(config);
            return responseToAsyncResult(std::move(response));
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

            validateHeaders(config.headers);
            auto result = client_->download(config);
            return downloadResultToAsyncResult(std::move(result));
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
            size_t headerCount = jsiNumArgs.size() > 1 ? static_cast<size_t>(jsiNumArgs.at(1)) : 0;
            size_t formFieldCount = jsiNumArgs.size() > 2 ? static_cast<size_t>(jsiNumArgs.at(2)) : 0;

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

            validateHeaders(config.headers);
            validateMultipart(config.fieldName); validateMultipart(config.fileName.empty() ? std::filesystem::path(config.filePath).filename().string() : config.fileName); validateMultipart(config.mimeType);
            for (const auto& [key, value] : config.formFields) { (void)value; validateMultipart(key); }
            auto result = client_->upload(config);
            return uploadResultToAsyncResult(std::move(result));
        })
    }
};

} // namespace rct_io

#endif // IO_REQUEST_HOST_OBJECT_HPP
