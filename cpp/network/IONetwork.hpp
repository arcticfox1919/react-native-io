/**
 * @file IONetwork.hpp
 * @brief IO library network module unified entry
 *
 * This is the main entry header for the IO library network module.
 * Include this file to use all network IO features.
 *
 * @example Basic usage
 * ```cpp
 * #include "IONetwork.hpp"
 *
 * using namespace rct_io::network;
 *
 * auto client = IOHttpClient::create();
 *
 * // GET request
 * auto response = client->get("https://api.example.com/data");
 *
 * // POST request
 * auto postResponse = client->post(
 *     "https://api.example.com/submit",
 *     R"({"key": "value"})",
 *     {{"Content-Type", "application/json"}}
 * );
 *
 * // File download with progress
 * DownloadConfig config;
 * config.url = "https://example.com/file.zip";
 * config.destinationPath = "/path/to/file.zip";
 *
 * auto result = client->download(config, [](DownloadProgress p) {
 *     std::cout << p.progress * 100 << "%" << std::endl;
 * });
 * ```
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
#include "IOHttpClient.hpp"

#ifdef __ANDROID__
#include "IOHttpClientAndroid.hpp"
#elif defined(__APPLE__)
#include "IOHttpClientIOS.hpp"
#endif

namespace rct_io::network {

inline constexpr const char* getNetworkVersion() {
    return "1.0.0";
}

inline constexpr const char* getPlatformName() {
#ifdef __ANDROID__
    return "Android";
#elif defined(__APPLE__)
    return "iOS";
#else
    return "Unknown";
#endif
}

} // namespace rct_io::network
