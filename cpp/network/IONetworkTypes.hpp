/**
 * @file IONetworkTypes.hpp
 * @brief Common type definitions for network IO module
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

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstdint>

namespace rct_io::network {

// ============================================================================
// HTTP Method
// ============================================================================

enum class HttpMethod {
    GET,
    POST,
    PUT,
    DELETE,
    PATCH,
    HEAD,
    OPTIONS
};

inline std::string httpMethodToString(HttpMethod method) {
    switch (method) {
        case HttpMethod::GET:     return "GET";
        case HttpMethod::POST:    return "POST";
        case HttpMethod::PUT:     return "PUT";
        case HttpMethod::DELETE:  return "DELETE";
        case HttpMethod::PATCH:   return "PATCH";
        case HttpMethod::HEAD:    return "HEAD";
        case HttpMethod::OPTIONS: return "OPTIONS";
        default:                  return "GET";
    }
}

inline HttpMethod stringToHttpMethod(const std::string& str) {
    if (str == "POST")    return HttpMethod::POST;
    if (str == "PUT")     return HttpMethod::PUT;
    if (str == "DELETE")  return HttpMethod::DELETE;
    if (str == "PATCH")   return HttpMethod::PATCH;
    if (str == "HEAD")    return HttpMethod::HEAD;
    if (str == "OPTIONS") return HttpMethod::OPTIONS;
    return HttpMethod::GET;
}

// ============================================================================
// HTTP Headers
// ============================================================================

using HttpHeaders = std::unordered_map<std::string, std::string>;

// ============================================================================
// HTTP Request Config
// ============================================================================

struct HttpRequestConfig {
    std::string url;
    HttpMethod method = HttpMethod::GET;
    HttpHeaders headers;
    std::vector<uint8_t> body;
    std::string bodyString;
    int32_t timeoutMs = 30000;
    bool followRedirects = true;

    [[nodiscard]] std::vector<uint8_t> getBodyBytes() const {
        if (!body.empty()) {
            return body;
        }
        return std::vector<uint8_t>(bodyString.begin(), bodyString.end());
    }

    [[nodiscard]] bool hasBody() const {
        return !body.empty() || !bodyString.empty();
    }
};

// ============================================================================
// HTTP Response
// ============================================================================

struct HttpResponse {
    int32_t statusCode = 0;
    std::string statusMessage;
    HttpHeaders headers;
    std::vector<uint8_t> body;
    std::string url;
    bool success = false;
    std::string errorMessage;

    [[nodiscard]] std::string bodyAsString() const {
        return std::string(body.begin(), body.end());
    }

    [[nodiscard]] bool isSuccessStatusCode() const {
        return statusCode >= 200 && statusCode < 300;
    }
};

// ============================================================================
// Progress Info
// ============================================================================

struct DownloadProgress {
    int64_t bytesReceived = 0;
    int64_t totalBytes = -1;
    double progress = 0.0;
};

struct UploadProgress {
    int64_t bytesSent = 0;
    int64_t totalBytes = -1;
    double progress = 0.0;
};

using DownloadProgressCallback = std::function<void(const DownloadProgress&)>;
using UploadProgressCallback = std::function<void(const UploadProgress&)>;

// ============================================================================
// Download Config & Result
// ============================================================================

struct DownloadConfig {
    std::string url;
    std::string destinationPath;
    HttpHeaders headers;
    int32_t timeoutMs = 60000;
    bool resumable = false;
};

struct DownloadResult {
    bool success = false;
    std::string filePath;
    int64_t fileSize = 0;
    int32_t statusCode = 0;
    std::string errorMessage;
};

// ============================================================================
// Upload Config & Result
// ============================================================================

struct UploadConfig {
    std::string url;
    std::string filePath;
    std::string fieldName = "file";
    std::string fileName;
    std::string mimeType;
    HttpHeaders headers;
    std::unordered_map<std::string, std::string> formFields;
    int32_t timeoutMs = 60000;
};

struct UploadResult {
    bool success = false;
    int32_t statusCode = 0;
    std::vector<uint8_t> responseBody;
    std::string errorMessage;

    [[nodiscard]] std::string responseAsString() const {
        return std::string(responseBody.begin(), responseBody.end());
    }
};

// ============================================================================
// HTTP Error Type
// ============================================================================

enum class HttpErrorType {
    None,
    Timeout,
    ConnectionFailed,
    DNSResolutionFailed,
    SSLError,
    Cancelled,
    InvalidURL,
    InvalidResponse,
    Unknown
};

inline std::string httpErrorTypeToString(HttpErrorType type) {
    switch (type) {
        case HttpErrorType::None:               return "None";
        case HttpErrorType::Timeout:            return "Timeout";
        case HttpErrorType::ConnectionFailed:   return "ConnectionFailed";
        case HttpErrorType::DNSResolutionFailed: return "DNSResolutionFailed";
        case HttpErrorType::SSLError:           return "SSLError";
        case HttpErrorType::Cancelled:          return "Cancelled";
        case HttpErrorType::InvalidURL:         return "InvalidURL";
        case HttpErrorType::InvalidResponse:    return "InvalidResponse";
        default:                                return "Unknown";
    }
}

} // namespace rct_io::network
