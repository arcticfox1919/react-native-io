/**
 * @file IOHttpClientAndroid.hpp
 * @brief Android HTTP client implementation header
 *
 * Uses fbjni high-level API for clean JNI bindings.
 * No raw JNI reflection or manual ClassLoader management needed.
 * fbjni automatically handles thread attachment and class caching.
 *
 * Copyright (c) 2025 arcticfox
 */

#pragma once

#ifdef __ANDROID__

#include "IONetwork.hpp"
#include <fbjni/fbjni.h>
#include <string>
#include <vector>
#include <map>

namespace rct_io::network {

using namespace facebook::jni;

// ============================================================================
// fbjni Wrapper Classes
// ============================================================================

/**
 * fbjni wrapper for Java HttpResult class.
 *
 * Provides type-safe access to Java object fields.
 * Field access is automatically cached by fbjni across multiple calls.
 */
class JHttpResult : public JavaClass<JHttpResult> {
public:
    static constexpr const char* kJavaDescriptor =
        "Lxyz/bczl/io/IOHttpClient$HttpResult;";

    bool isSuccess();
    int getStatusCode();
    std::string getStatusMessage();
    std::vector<uint8_t> getBody();
    HttpHeaders getHeaders();
    std::string getFinalUrl();
    std::string getErrorMessage();
};

/**
 * fbjni wrapper for Java DownloadResult class.
 */
class JDownloadResult : public JavaClass<JDownloadResult> {
public:
    static constexpr const char* kJavaDescriptor =
        "Lxyz/bczl/io/IOHttpClient$DownloadResult;";

    bool isSuccess();
    int getStatusCode();
    std::string getFilePath();
    int64_t getFileSize();
    std::string getErrorMessage();
};

/**
 * fbjni wrapper for Java UploadResult class.
 */
class JUploadResult : public JavaClass<JUploadResult> {
public:
    static constexpr const char* kJavaDescriptor =
        "Lxyz/bczl/io/IOHttpClient$UploadResult;";

    bool isSuccess();
    int getStatusCode();
    std::vector<uint8_t> getResponseBody();
    std::string getErrorMessage();
};

/**
 * fbjni wrapper for Java IOHttpClient static methods.
 *
 * fbjni automatically handles:
 * - Thread attachment to JVM
 * - Correct ClassLoader context
 * - Exception translation
 * - Class/method caching
 *
 * No manual JNI reflection or global reference management needed.
 */
class JIOHttpClient : public JavaClass<JIOHttpClient> {
public:
    static constexpr const char* kJavaDescriptor = "Lxyz/bczl/io/IOHttpClient;";

    static local_ref<JHttpResult> request(
        const std::string& url,
        const std::string& method,
        const std::vector<std::string>& headerKeys,
        const std::vector<std::string>& headerValues,
        const std::vector<uint8_t>& body,
        long timeoutMs,
        bool followRedirects
    );

    static local_ref<JDownloadResult> download(
        const std::string& url,
        const std::string& destinationPath,
        const std::vector<std::string>& headerKeys,
        const std::vector<std::string>& headerValues,
        long timeoutMs,
        bool resumable
    );

    static local_ref<JUploadResult> upload(
        const std::string& url,
        const std::string& filePath,
        const std::string& fieldName,
        const std::string& fileName,
        const std::string& mimeType,
        const std::vector<std::string>& headerKeys,
        const std::vector<std::string>& headerValues,
        const std::vector<std::string>& formKeys,
        const std::vector<std::string>& formValues,
        long timeoutMs
    );
};

// ============================================================================
// Android HTTP Client Implementation
// ============================================================================

/**
 * Android implementation of IOHttpClient using Java IOHttpClient via fbjni.
 *
 * Completely async and non-blocking. All calls delegate to Java IOHttpClient
 * static methods through the fbjni wrapper classes above.
 */
class IOHttpClientAndroid : public IOHttpClient {
public:
    HttpResponse request(const HttpRequestConfig& config) override;
    DownloadResult download(const DownloadConfig& config,
                            DownloadProgressCallback progressCallback = nullptr) override;
    UploadResult upload(const UploadConfig& config,
                        UploadProgressCallback progressCallback = nullptr) override;
};

/**
 * Pre-warm fbjni class caches for HTTP client classes.
 * Should be called from the main thread before using HTTP client.
 * This ensures class references are initialized with the correct ClassLoader.
 */
void installHttpClientCaches();

} // namespace rct_io::network

#endif // __ANDROID__
