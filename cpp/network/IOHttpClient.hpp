/**
 * @file IOHttpClient.hpp
 * @brief Unified HTTP client interface for IO library
 *
 * IOHttpClient is the unified entry point for network IO module,
 * providing cross-platform HTTP/HTTPS request capabilities.
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

#include "IONetworkTypes.hpp"
#include <memory>

namespace rct_io::network {

/**
 * @brief Unified HTTP client entry class
 *
 * Platform-specific implementation is automatically selected:
 *   - Android: Uses fbjni to reflect HttpURLConnection
 *   - iOS: Uses NSURLSession
 *
 * @example Basic GET request
 * ```cpp
 * auto client = IOHttpClient::create();
 * auto response = client->get("https://api.example.com/data");
 * if (response.success) {
 *     std::string data = response.bodyAsString();
 * }
 * ```
 *
 * @example POST request with JSON
 * ```cpp
 * HttpRequestConfig config;
 * config.url = "https://api.example.com/submit";
 * config.method = HttpMethod::POST;
 * config.bodyString = R"({"key": "value"})";
 * config.headers["Content-Type"] = "application/json";
 *
 * auto response = client->request(config);
 * ```
 */
class IOHttpClient {
public:
    virtual ~IOHttpClient() = default;

    /**
     * @brief Create platform-specific HTTP client instance
     * @return Platform-specific IOHttpClient implementation
     */
    static std::shared_ptr<IOHttpClient> create();

    /**
     * @brief Execute HTTP request
     * @param config Request configuration
     * @return HTTP response
     */
    virtual HttpResponse request(const HttpRequestConfig& config) = 0;

    /**
     * @brief Execute GET request
     * @param url Request URL
     * @param headers Optional request headers
     * @return HTTP response
     */
    HttpResponse get(const std::string& url, const HttpHeaders& headers = {}) {
        HttpRequestConfig config;
        config.url = url;
        config.method = HttpMethod::GET;
        config.headers = headers;
        return request(config);
    }

    /**
     * @brief Execute POST request
     * @param url Request URL
     * @param body Request body
     * @param headers Optional request headers
     * @return HTTP response
     */
    HttpResponse post(const std::string& url,
                      const std::string& body,
                      const HttpHeaders& headers = {}) {
        HttpRequestConfig config;
        config.url = url;
        config.method = HttpMethod::POST;
        config.bodyString = body;
        config.headers = headers;
        return request(config);
    }

    /**
     * @brief Execute PUT request
     */
    HttpResponse put(const std::string& url,
                     const std::string& body,
                     const HttpHeaders& headers = {}) {
        HttpRequestConfig config;
        config.url = url;
        config.method = HttpMethod::PUT;
        config.bodyString = body;
        config.headers = headers;
        return request(config);
    }

    /**
     * @brief Execute DELETE request
     */
    HttpResponse del(const std::string& url, const HttpHeaders& headers = {}) {
        HttpRequestConfig config;
        config.url = url;
        config.method = HttpMethod::DELETE;
        config.headers = headers;
        return request(config);
    }

    /**
     * @brief Download file to specified path
     * @param config Download configuration
     * @param progressCallback Optional progress callback
     * @return Download result
     */
    virtual DownloadResult download(const DownloadConfig& config,
                                    DownloadProgressCallback progressCallback = nullptr) = 0;

    /**
     * @brief Upload file using multipart/form-data
     * @param config Upload configuration
     * @param progressCallback Optional progress callback
     * @return Upload result
     */
    virtual UploadResult upload(const UploadConfig& config,
                                UploadProgressCallback progressCallback = nullptr) = 0;

protected:
    IOHttpClient() = default;
};

/** @brief Convenient alias for IOHttpClient */
using IONetworkIO = IOHttpClient;

} // namespace rct_io::network
