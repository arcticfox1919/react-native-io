/**
 * @file IOHttpClientAndroid.cpp
 * @brief Android platform HTTP client implementation
 *
 * Uses fbjni high-level API to call Java IOHttpClient.
 * Class caches are pre-warmed from Java main thread via installIOHttpClient.
 *
 * Copyright (c) 2025 arcticfox
 */

#ifdef __ANDROID__

#include "IOHttpClientAndroid.hpp"
#include "../src/Logger.h"
#include <atomic>

namespace {
constexpr const char* TAG = "IOHttpClient";
}

namespace rct_io::network {

// ============================================================================
// JHttpResult Implementation (uses fbjni cached class)
// ============================================================================

bool JHttpResult::isSuccess() {
    static const auto field = javaClassStatic()->getField<jboolean>("success");
    return getFieldValue(field);
}

int JHttpResult::getStatusCode() {
    static const auto field = javaClassStatic()->getField<jint>("statusCode");
    return getFieldValue(field);
}

std::string JHttpResult::getStatusMessage() {
    static const auto field = javaClassStatic()->getField<JString>("statusMessage");
    auto value = getFieldValue(field);
    return value ? value->toStdString() : "";
}

std::vector<uint8_t> JHttpResult::getBody() {
    static const auto field = javaClassStatic()->getField<jbyteArray>("body");
    auto arr = getFieldValue(field);
    if (!arr) return {};

    auto size = arr->size();
    std::vector<uint8_t> result(size);
    if (size > 0) {
        arr->getRegion(0, size, reinterpret_cast<jbyte*>(result.data()));
    }
    return result;
}

HttpHeaders JHttpResult::getHeaders() {
    HttpHeaders headers;
    static const auto keysField = javaClassStatic()->getField<JArrayClass<JString>>("headerKeys");
    static const auto valuesField = javaClassStatic()->getField<JArrayClass<JString>>("headerValues");

    auto keys = getFieldValue(keysField);
    auto values = getFieldValue(valuesField);

    if (keys && values) {
        size_t size = std::min(keys->size(), values->size());
        for (size_t i = 0; i < size; ++i) {
            auto key = keys->getElement(i);
            auto value = values->getElement(i);
            if (key && value) {
                headers[key->toStdString()] = value->toStdString();
            }
        }
    }
    return headers;
}

std::string JHttpResult::getFinalUrl() {
    static const auto field = javaClassStatic()->getField<JString>("finalUrl");
    auto value = getFieldValue(field);
    return value ? value->toStdString() : "";
}

std::string JHttpResult::getErrorMessage() {
    static const auto field = javaClassStatic()->getField<JString>("errorMessage");
    auto value = getFieldValue(field);
    return value ? value->toStdString() : "";
}

// ============================================================================
// JDownloadResult Implementation
// ============================================================================

bool JDownloadResult::isSuccess() {
    static const auto field = javaClassStatic()->getField<jboolean>("success");
    return getFieldValue(field);
}

int JDownloadResult::getStatusCode() {
    static const auto field = javaClassStatic()->getField<jint>("statusCode");
    return getFieldValue(field);
}

std::string JDownloadResult::getFilePath() {
    static const auto field = javaClassStatic()->getField<JString>("filePath");
    auto value = getFieldValue(field);
    return value ? value->toStdString() : "";
}

int64_t JDownloadResult::getFileSize() {
    static const auto field = javaClassStatic()->getField<jlong>("fileSize");
    return getFieldValue(field);
}

std::string JDownloadResult::getErrorMessage() {
    static const auto field = javaClassStatic()->getField<JString>("errorMessage");
    auto value = getFieldValue(field);
    return value ? value->toStdString() : "";
}

// ============================================================================
// JUploadResult Implementation
// ============================================================================

bool JUploadResult::isSuccess() {
    static const auto field = javaClassStatic()->getField<jboolean>("success");
    return getFieldValue(field);
}

int JUploadResult::getStatusCode() {
    static const auto field = javaClassStatic()->getField<jint>("statusCode");
    return getFieldValue(field);
}

std::vector<uint8_t> JUploadResult::getResponseBody() {
    static const auto field = javaClassStatic()->getField<jbyteArray>("responseBody");
    auto arr = getFieldValue(field);
    if (!arr) return {};

    auto size = arr->size();
    std::vector<uint8_t> result(size);
    if (size > 0) {
        arr->getRegion(0, size, reinterpret_cast<jbyte*>(result.data()));
    }
    return result;
}

std::string JUploadResult::getErrorMessage() {
    static const auto field = javaClassStatic()->getField<JString>("errorMessage");
    auto value = getFieldValue(field);
    return value ? value->toStdString() : "";
}

// ============================================================================
// JIOHttpClient Implementation - Call Java methods via fbjni
// ============================================================================

namespace {
    // Helper: Convert std::vector<std::string> to Java String[]
    local_ref<JArrayClass<JString>> toJStringArray(const std::vector<std::string>& vec) {
        auto arr = JArrayClass<JString>::newArray(vec.size());
        for (size_t i = 0; i < vec.size(); ++i) {
            arr->setElement(i, *make_jstring(vec[i]));
        }
        return arr;
    }

    // Helper: Convert std::vector<uint8_t> to Java byte[]
    local_ref<JArrayByte> toJByteArray(const std::vector<uint8_t>& vec) {
        auto arr = JArrayByte::newArray(vec.size());
        if (!vec.empty()) {
            arr->setRegion(0, vec.size(), reinterpret_cast<const jbyte*>(vec.data()));
        }
        return arr;
    }
}

local_ref<JHttpResult> JIOHttpClient::request(
        const std::string& url,
        const std::string& method,
        const std::vector<std::string>& headerKeys,
        const std::vector<std::string>& headerValues,
        const std::vector<uint8_t>& body,
        long timeoutMs,
        bool followRedirects) {

    rct_io::Logger::d(TAG, "JIOHttpClient::request() - url=%s, method=%s", url.c_str(), method.c_str());

    // Use fbjni's getStaticMethod - class cache was pre-warmed in installIOHttpClient
    static const auto requestMethod = JIOHttpClient::javaClassStatic()
        ->getStaticMethod<JHttpResult(
            JString, JString,
            JArrayClass<JString>, JArrayClass<JString>,
            JArrayByte, jint, jboolean)>("request");

    auto jUrl = make_jstring(url);
    auto jMethod = make_jstring(method);
    auto jHeaderKeys = toJStringArray(headerKeys);
    auto jHeaderValues = toJStringArray(headerValues);
    auto jBody = toJByteArray(body);

    return requestMethod(
        JIOHttpClient::javaClassStatic(),
        *jUrl,
        *jMethod,
        *jHeaderKeys,
        *jHeaderValues,
        *jBody,
        static_cast<jint>(timeoutMs),
        static_cast<jboolean>(followRedirects)
    );
}

local_ref<JDownloadResult> JIOHttpClient::download(
        const std::string& url,
        const std::string& destinationPath,
        const std::vector<std::string>& headerKeys,
        const std::vector<std::string>& headerValues,
        long timeoutMs,
        bool resumable) {

    rct_io::Logger::d(TAG, "JIOHttpClient::download() - url=%s", url.c_str());

    static const auto downloadMethod = JIOHttpClient::javaClassStatic()
        ->getStaticMethod<JDownloadResult(
            JString, JString,
            JArrayClass<JString>, JArrayClass<JString>,
            jint, jboolean)>("download");

    auto jUrl = make_jstring(url);
    auto jDestPath = make_jstring(destinationPath);
    auto jHeaderKeys = toJStringArray(headerKeys);
    auto jHeaderValues = toJStringArray(headerValues);

    return downloadMethod(
        JIOHttpClient::javaClassStatic(),
        *jUrl,
        *jDestPath,
        *jHeaderKeys,
        *jHeaderValues,
        static_cast<jint>(timeoutMs),
        static_cast<jboolean>(resumable)
    );
}

local_ref<JUploadResult> JIOHttpClient::upload(
        const std::string& url,
        const std::string& filePath,
        const std::string& fieldName,
        const std::string& fileName,
        const std::string& mimeType,
        const std::vector<std::string>& headerKeys,
        const std::vector<std::string>& headerValues,
        const std::vector<std::string>& formKeys,
        const std::vector<std::string>& formValues,
        long timeoutMs) {

    rct_io::Logger::d(TAG, "JIOHttpClient::upload() - filePath=%s", filePath.c_str());

    static const auto uploadMethod = JIOHttpClient::javaClassStatic()
        ->getStaticMethod<JUploadResult(
            JString, JString, JString, JString, JString,
            JArrayClass<JString>, JArrayClass<JString>,
            JArrayClass<JString>, JArrayClass<JString>,
            jint)>("upload");

    auto jUrl = make_jstring(url);
    auto jFilePath = make_jstring(filePath);
    auto jFieldName = make_jstring(fieldName);
    auto jFileName = make_jstring(fileName);
    auto jMimeType = make_jstring(mimeType);
    auto jHeaderKeys = toJStringArray(headerKeys);
    auto jHeaderValues = toJStringArray(headerValues);
    auto jFormKeys = toJStringArray(formKeys);
    auto jFormValues = toJStringArray(formValues);

    return uploadMethod(
        JIOHttpClient::javaClassStatic(),
        *jUrl,
        *jFilePath,
        *jFieldName,
        *jFileName,
        *jMimeType,
        *jHeaderKeys,
        *jHeaderValues,
        *jFormKeys,
        *jFormValues,
        static_cast<jint>(timeoutMs)
    );
}

// ============================================================================
// IOHttpClientAndroid Implementation - Use fbjni wrappers
// ============================================================================

HttpResponse IOHttpClientAndroid::request(const HttpRequestConfig& config) {
    // Ensure thread is attached to JVM for the entire operation
    ThreadScope threadScope;

    HttpResponse response;

    try {
        rct_io::Logger::d(TAG, "Starting request to: %s", config.url.c_str());

        std::vector<std::string> headerKeys, headerValues;
        for (const auto& [k, v] : config.headers) {
            headerKeys.push_back(k);
            headerValues.push_back(v);
        }

        auto jResult = JIOHttpClient::request(
            config.url,
            httpMethodToString(config.method),
            headerKeys,
            headerValues,
            config.getBodyBytes(),
            config.timeoutMs,
            config.followRedirects
        );

        if (jResult) {
            response.success = jResult->isSuccess();
            response.statusCode = jResult->getStatusCode();
            response.statusMessage = jResult->getStatusMessage();
            response.url = jResult->getFinalUrl();
            response.errorMessage = jResult->getErrorMessage();
            response.body = jResult->getBody();
            response.headers = jResult->getHeaders();

            rct_io::Logger::d(TAG, "Response: success=%d, statusCode=%d, bodySize=%zu",
                 response.success, response.statusCode, response.body.size());
        } else {
            response.errorMessage = "Java returned null result";
            rct_io::Logger::e(TAG, "Java returned null result");
        }

    } catch (const std::exception& e) {
        response.errorMessage = std::string("Exception: ") + e.what();
        rct_io::Logger::e(TAG, "Exception: %s", e.what());
    }

    return response;
}

DownloadResult IOHttpClientAndroid::download(const DownloadConfig& config,
                                              DownloadProgressCallback progressCallback) {
    // Ensure thread is attached to JVM for the entire operation
    ThreadScope threadScope;

    DownloadResult result;

    try {
        rct_io::Logger::d(TAG, "Starting download from: %s to: %s", config.url.c_str(), config.destinationPath.c_str());

        std::vector<std::string> headerKeys, headerValues;
        for (const auto& [k, v] : config.headers) {
            headerKeys.push_back(k);
            headerValues.push_back(v);
        }

        auto jResult = JIOHttpClient::download(
            config.url,
            config.destinationPath,
            headerKeys,
            headerValues,
            config.timeoutMs,
            config.resumable
        );

        if (jResult) {
            result.success = jResult->isSuccess();
            result.statusCode = jResult->getStatusCode();
            result.filePath = jResult->getFilePath();
            result.fileSize = jResult->getFileSize();
            result.errorMessage = jResult->getErrorMessage();

            rct_io::Logger::d(TAG, "Download: success=%d, statusCode=%d, fileSize=%ld",
                 result.success, result.statusCode, (long)result.fileSize);
        } else {
            result.errorMessage = "Java returned null result";
            rct_io::Logger::e(TAG, "Java returned null result");
        }

    } catch (const std::exception& e) {
        result.errorMessage = std::string("Exception: ") + e.what();
        rct_io::Logger::e(TAG, "Exception: %s", e.what());
    }

    return result;
}

UploadResult IOHttpClientAndroid::upload(const UploadConfig& config,
                                          UploadProgressCallback progressCallback) {
    // Ensure thread is attached to JVM for the entire operation
    ThreadScope threadScope;

    UploadResult result;

    try {
        rct_io::Logger::d(TAG, "Starting upload of: %s", config.filePath.c_str());

        std::vector<std::string> headerKeys, headerValues;
        for (const auto& [k, v] : config.headers) {
            headerKeys.push_back(k);
            headerValues.push_back(v);
        }

        std::vector<std::string> formKeys, formValues;
        for (const auto& [k, v] : config.formFields) {
            formKeys.push_back(k);
            formValues.push_back(v);
        }

        auto jResult = JIOHttpClient::upload(
            config.url,
            config.filePath,
            config.fieldName,
            config.fileName,
            config.mimeType,
            headerKeys,
            headerValues,
            formKeys,
            formValues,
            config.timeoutMs
        );

        if (jResult) {
            result.success = jResult->isSuccess();
            result.statusCode = jResult->getStatusCode();
            result.responseBody = jResult->getResponseBody();
            result.errorMessage = jResult->getErrorMessage();

            rct_io::Logger::d(TAG, "Upload: success=%d, statusCode=%d, responseSize=%zu",
                 result.success, result.statusCode, result.responseBody.size());
        } else {
            result.errorMessage = "Java returned null result";
            rct_io::Logger::e(TAG, "Java returned null result");
        }

    } catch (const std::exception& e) {
        result.errorMessage = std::string("Exception: ") + e.what();
        rct_io::Logger::e(TAG, "Exception: %s", e.what());
    }

    return result;
}

std::shared_ptr<IOHttpClient> IOHttpClient::create() {
    return std::make_shared<IOHttpClientAndroid>();
}

} // namespace rct_io::network

// ============================================================================
// JNI Native Methods for Progress Callbacks
// ============================================================================

extern "C" {

JNIEXPORT void JNICALL
Java_xyz_bczl_io_IOHttpClient_nativeDownloadProgress(
        JNIEnv* /*env*/,
        jclass /*clazz*/,
        jlong callback,
        jlong current,
        jlong total,
        jdouble progress) {

    if (callback != 0) {
        auto* cb = reinterpret_cast<rct_io::network::DownloadProgressCallback*>(callback);
        if (*cb) {
            rct_io::network::DownloadProgress progressInfo;
            progressInfo.bytesReceived = current;
            progressInfo.totalBytes = total;
            progressInfo.progress = progress;
            (*cb)(progressInfo);
        }
    }
}

JNIEXPORT void JNICALL
Java_xyz_bczl_io_IOHttpClient_nativeUploadProgress(
        JNIEnv* /*env*/,
        jclass /*clazz*/,
        jlong callback,
        jlong current,
        jlong total,
        jdouble progress) {

    if (callback != 0) {
        auto* cb = reinterpret_cast<rct_io::network::UploadProgressCallback*>(callback);
        if (*cb) {
            rct_io::network::UploadProgress progressInfo;
            progressInfo.bytesSent = current;
            progressInfo.totalBytes = total;
            progressInfo.progress = progress;
            (*cb)(progressInfo);
        }
    }
}

} // extern "C"

// ============================================================================
// Pre-warm fbjni class caches (called from TurboModule)
// ============================================================================

namespace rct_io::network {

void installHttpClientCaches() {
    rct_io::Logger::d(TAG, "installHttpClientCaches called - pre-warming fbjni class caches");

    try {
        // Pre-warm fbjni class caches by calling javaClassStatic()
        // This ensures the static class references are initialized with correct ClassLoader

        rct_io::Logger::d(TAG, "Pre-warming JIOHttpClient class cache...");
        auto ioHttpClientCls = JIOHttpClient::javaClassStatic();
        rct_io::Logger::d(TAG, "JIOHttpClient class: %p", ioHttpClientCls.get());

        rct_io::Logger::d(TAG, "Pre-warming JHttpResult class cache...");
        auto httpResultCls = JHttpResult::javaClassStatic();
        rct_io::Logger::d(TAG, "JHttpResult class: %p", httpResultCls.get());

        rct_io::Logger::d(TAG, "Pre-warming JDownloadResult class cache...");
        auto downloadResultCls = JDownloadResult::javaClassStatic();
        rct_io::Logger::d(TAG, "JDownloadResult class: %p", downloadResultCls.get());

        rct_io::Logger::d(TAG, "Pre-warming JUploadResult class cache...");
        auto uploadResultCls = JUploadResult::javaClassStatic();
        rct_io::Logger::d(TAG, "JUploadResult class: %p", uploadResultCls.get());

        rct_io::Logger::d(TAG, "installHttpClientCaches completed successfully - all fbjni class caches warmed");

    } catch (const std::exception& e) {
        rct_io::Logger::e(TAG, "installHttpClientCaches exception: %s", e.what());
    }
}

} // namespace rct_io::network

#endif // __ANDROID__
