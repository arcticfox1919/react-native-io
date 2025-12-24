/**
 * @file IOHttpClientIOS.hpp
 * @brief iOS platform HTTP client implementation
 *
 * Uses NSURLSession for HTTP requests
 *
 * Copyright (c) 2025 arcticfox
 */

#pragma once

#if defined(__APPLE__) && defined(__OBJC__)

#include "IOHttpClient.hpp"
#import <Foundation/Foundation.h>

namespace rct_io::network {

/**
 * @brief iOS platform HTTP client implementation using NSURLSession
 */
class IOHttpClientIOS : public IOHttpClient {
public:
    IOHttpClientIOS();
    ~IOHttpClientIOS() override;

    IOHttpClientIOS(const IOHttpClientIOS&) = delete;
    IOHttpClientIOS& operator=(const IOHttpClientIOS&) = delete;

    HttpResponse request(const HttpRequestConfig& config) override;
    DownloadResult download(const DownloadConfig& config,
                            DownloadProgressCallback progressCallback = nullptr) override;
    UploadResult upload(const UploadConfig& config,
                        UploadProgressCallback progressCallback = nullptr) override;

private:
    NSURLSession* session_;
    NSOperationQueue* delegateQueue_;

    NSMutableURLRequest* createRequest(const HttpRequestConfig& config);
    HttpResponse convertResponse(NSHTTPURLResponse* response, NSData* data, NSError* error);
    NSData* createMultipartBody(const UploadConfig& config, NSString** boundary);
};

} // namespace rct_io::network

#endif // __APPLE__ && __OBJC__
