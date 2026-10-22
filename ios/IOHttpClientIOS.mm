/**
 * @file IOHttpClientIOS.mm
 * @brief iOS platform HTTP client implementation
 *
 * Uses NSURLSession for HTTP requests
 *
 * Copyright (c) 2025 arcticfox
 */

#if defined(__APPLE__)

#include "IOHttpClientIOS.hpp"
#import <Foundation/Foundation.h>
#include <dispatch/dispatch.h>
#include <fstream>
#include <array>
#include <filesystem>
#include "../cpp/src/IOLimits.hpp"

// ============================================================================
// Sync Request Delegate
// ============================================================================

@interface IOSyncRequestDelegate : NSObject <NSURLSessionDataDelegate>
@property (nonatomic, strong) NSMutableData* receivedData;
@property (nonatomic, strong) NSHTTPURLResponse* response;
@property (nonatomic, strong) NSError* error;
@property (nonatomic, assign) BOOL completed;
@property (nonatomic, assign) BOOL followRedirects;
@property (nonatomic, strong) dispatch_semaphore_t semaphore;
@property (nonatomic, copy) void (^progressCallback)(int64_t, int64_t);
@end

@implementation IOSyncRequestDelegate

- (instancetype)init {
    if (self = [super init]) {
        _receivedData = [NSMutableData data];
        _followRedirects = YES;
        _completed = NO;
        _semaphore = dispatch_semaphore_create(0);
    }
    return self;
}

- (void)URLSession:(NSURLSession *)session
          dataTask:(NSURLSessionDataTask *)dataTask
didReceiveResponse:(NSURLResponse *)response
 completionHandler:(void (^)(NSURLSessionResponseDisposition))completionHandler {
    if ([response isKindOfClass:[NSHTTPURLResponse class]]) {
        self.response = (NSHTTPURLResponse *)response;
    }
    completionHandler(NSURLSessionResponseAllow);
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task
 willPerformHTTPRedirection:(NSHTTPURLResponse *)response newRequest:(NSURLRequest *)request
 completionHandler:(void (^)(NSURLRequest *))completionHandler {
    completionHandler(self.followRedirects ? request : nil);
}

- (void)URLSession:(NSURLSession *)session
          dataTask:(NSURLSessionDataTask *)dataTask
    didReceiveData:(NSData *)data {
    if (data.length > rct_io::maxBufferBytes - self.receivedData.length) {
        self.error = [NSError errorWithDomain:@"react-native-io" code:1 userInfo:@{NSLocalizedDescriptionKey: @"HTTP response exceeds buffer limit; use download"}];
        [dataTask cancel];
        return;
    }
    [self.receivedData appendData:data];

    if (self.progressCallback) {
        int64_t totalBytesReceived = self.receivedData.length;
        int64_t expectedBytes = self.response.expectedContentLength;
        self.progressCallback(totalBytesReceived, expectedBytes);
    }
}

- (void)URLSession:(NSURLSession *)session
              task:(NSURLSessionTask *)task
didCompleteWithError:(NSError *)error {
    if (error) self.error = error;
    self.completed = YES;
    dispatch_semaphore_signal(self.semaphore);
}

- (void)URLSession:(NSURLSession *)session
              task:(NSURLSessionTask *)task
   didSendBodyData:(int64_t)bytesSent
    totalBytesSent:(int64_t)totalBytesSent
totalBytesExpectedToSend:(int64_t)totalBytesExpectedToSend {
    if (self.progressCallback) {
        self.progressCallback(totalBytesSent, totalBytesExpectedToSend);
    }
}

@end

// ============================================================================
// Download Delegate
// ============================================================================

@interface IODownloadDelegate : NSObject <NSURLSessionDownloadDelegate>
@property (nonatomic, copy) NSString* destinationPath;
@property (nonatomic, assign) BOOL completed;
@property (nonatomic, assign) BOOL success;
@property (nonatomic, assign) int64_t fileSize;
@property (nonatomic, assign) NSInteger statusCode;
@property (nonatomic, strong) NSError* error;
@property (nonatomic, strong) dispatch_semaphore_t semaphore;
@property (nonatomic, copy) void (^progressCallback)(int64_t, int64_t);
@end

@implementation IODownloadDelegate

- (instancetype)init {
    if (self = [super init]) {
        _completed = NO;
        _success = NO;
        _semaphore = dispatch_semaphore_create(0);
    }
    return self;
}

- (void)URLSession:(NSURLSession *)session
      downloadTask:(NSURLSessionDownloadTask *)downloadTask
didFinishDownloadingToURL:(NSURL *)location {

    NSHTTPURLResponse* response = (NSHTTPURLResponse*)downloadTask.response;
    self.statusCode = response.statusCode;

    if (self.statusCode < 200 || self.statusCode >= 300) {
        self.error = [NSError errorWithDomain:@"react-native-io" code:self.statusCode userInfo:@{NSLocalizedDescriptionKey: @"HTTP download failed"}];
        return;
    }
    NSError* moveError = nil;
    NSFileManager* fileManager = [NSFileManager defaultManager];
    NSURL* destURL = [NSURL fileURLWithPath:self.destinationPath];
    NSString* directory = [self.destinationPath stringByDeletingLastPathComponent];
    if (![fileManager createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:&moveError]) {
        self.error = moveError;
        return;
    }
    BOOL moved;
    if ([fileManager fileExistsAtPath:self.destinationPath]) {
        moved = [fileManager replaceItemAtURL:destURL withItemAtURL:location backupItemName:nil options:0 resultingItemURL:nil error:&moveError];
    } else {
        moved = [fileManager moveItemAtURL:location toURL:destURL error:&moveError];
    }
    self.success = moved;
    self.error = moveError;
    if (moved) self.fileSize = [[fileManager attributesOfItemAtPath:self.destinationPath error:nil] fileSize];
}

- (void)URLSession:(NSURLSession *)session
      downloadTask:(NSURLSessionDownloadTask *)downloadTask
      didWriteData:(int64_t)bytesWritten
 totalBytesWritten:(int64_t)totalBytesWritten
totalBytesExpectedToWrite:(int64_t)totalBytesExpectedToWrite {
    if (self.progressCallback) {
        self.progressCallback(totalBytesWritten, totalBytesExpectedToWrite);
    }
}

- (void)URLSession:(NSURLSession *)session
              task:(NSURLSessionTask *)task
didCompleteWithError:(NSError *)error {
    if (error) {
        self.error = error;
        self.success = NO;
    }
    self.completed = YES;
    dispatch_semaphore_signal(self.semaphore);
}

@end

static bool waitForCompletion(NSURLSessionTask* task,
                              NSURLSession* session,
                              dispatch_semaphore_t semaphore,
                              int32_t timeoutMs) {
    if (timeoutMs <= 0) return false;
    // URLSession should complete within its configured resource timeout. Keep
    // a small grace period for delivery of didCompleteWithError, but never
    // leave a native worker blocked forever if that callback is lost.
    const int64_t waitNanoseconds = static_cast<int64_t>(timeoutMs) * NSEC_PER_MSEC + 5LL * NSEC_PER_SEC;
    if (dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, waitNanoseconds)) == 0) return true;
    [task cancel];
    [session invalidateAndCancel];
    return false;
}

// ============================================================================
// IOHttpClientIOS Implementation
// ============================================================================

namespace rct_io::network {

IOHttpClientIOS::IOHttpClientIOS() = default;
IOHttpClientIOS::~IOHttpClientIOS() = default;

NSMutableURLRequest* IOHttpClientIOS::createRequest(const HttpRequestConfig& config) {
    @autoreleasepool {
        NSURL* url = [NSURL URLWithString:[NSString stringWithUTF8String:config.url.c_str()]];
        if (!url || !([url.scheme.lowercaseString isEqualToString:@"http"] || [url.scheme.lowercaseString isEqualToString:@"https"])) {
            return nil;
        }

        NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:url];
        request.HTTPMethod = [NSString stringWithUTF8String:httpMethodToString(config.method).c_str()];
        request.timeoutInterval = config.timeoutMs / 1000.0;

        for (const auto& [key, value] : config.headers) {
            [request setValue:[NSString stringWithUTF8String:value.c_str()]
           forHTTPHeaderField:[NSString stringWithUTF8String:key.c_str()]];
        }

        if (config.hasBody()) {
            auto bodyBytes = config.getBodyView();
            request.HTTPBody = [NSData dataWithBytes:bodyBytes.data() length:bodyBytes.size()];
        }

        return request;
    }
}

HttpResponse IOHttpClientIOS::convertResponse(NSHTTPURLResponse* response, NSData* data, NSError* error) {
    HttpResponse result;

    if (error) {
        result.success = false;
        result.errorMessage = error.localizedDescription.UTF8String;
        return result;
    }

    if (response) {
        result.statusCode = static_cast<int32_t>([response statusCode]);
        result.url = [[response URL] absoluteString].UTF8String;

        NSDictionary* headers = response.allHeaderFields;
        for (NSString* key in headers) {
            NSString* value = [headers[key] description];
            result.headers[[key UTF8String]] = [value UTF8String];
        }
    }

    if (data && [data length] > 0) {
        const uint8_t* bytes = static_cast<const uint8_t*>([data bytes]);
        result.body.assign(bytes, bytes + [data length]);
    }

    result.success = true;
    return result;
}

// ============================================================================
// HTTP Request
// ============================================================================

HttpResponse IOHttpClientIOS::request(const HttpRequestConfig& config) {
    @autoreleasepool {
        if (config.timeoutMs <= 0) {
            HttpResponse response;
            response.errorMessage = "timeoutMs must be greater than zero";
            return response;
        }
        NSMutableURLRequest* request = createRequest(config);
        if (!request) {
            HttpResponse response;
            response.errorMessage = "Invalid URL: " + config.url;
            return response;
        }

        IOSyncRequestDelegate* delegate = [[IOSyncRequestDelegate alloc] init];
        delegate.followRedirects = config.followRedirects;

        NSURLSessionConfiguration* sessionConfig = [NSURLSessionConfiguration defaultSessionConfiguration];
        sessionConfig.timeoutIntervalForRequest = config.timeoutMs / 1000.0;
        sessionConfig.timeoutIntervalForResource = config.timeoutMs / 1000.0;

        NSURLSession* session = [NSURLSession sessionWithConfiguration:sessionConfig
                                                              delegate:delegate
                                                         delegateQueue:nil];

        NSURLSessionDataTask* task = [session dataTaskWithRequest:request];
        [task resume];

        if (!waitForCompletion(task, session, delegate.semaphore, config.timeoutMs)) {
            HttpResponse response;
            response.errorMessage = "HTTP request exceeded its completion deadline";
            return response;
        }

        [session invalidateAndCancel];

        return convertResponse(delegate.response, delegate.receivedData, delegate.error);
    }
}

// ============================================================================
// File Download
// ============================================================================

DownloadResult IOHttpClientIOS::download(const DownloadConfig& config,
                                          DownloadProgressCallback progressCallback) {
    @autoreleasepool {
        DownloadResult result;

        if (config.timeoutMs <= 0) {
            result.errorMessage = "timeoutMs must be greater than zero";
            return result;
        }

        NSURL* url = [NSURL URLWithString:[NSString stringWithUTF8String:config.url.c_str()]];
        if (!url || !([url.scheme.lowercaseString isEqualToString:@"http"] || [url.scheme.lowercaseString isEqualToString:@"https"])) {
            result.errorMessage = "Invalid URL: " + config.url;
            return result;
        }

        NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:url];
        request.timeoutInterval = config.timeoutMs / 1000.0;

        for (const auto& [key, value] : config.headers) {
            [request setValue:[NSString stringWithUTF8String:value.c_str()]
           forHTTPHeaderField:[NSString stringWithUTF8String:key.c_str()]];
        }

        if (config.resumable) {
            [request setValue:nil forHTTPHeaderField:@"Range"];
            [request setValue:nil forHTTPHeaderField:@"If-Range"];
        }
        // Restart safely on iOS. A byte Range without an entity validator can
        // concatenate different remote versions or replace a file with only its tail.

        IODownloadDelegate* delegate = [[IODownloadDelegate alloc] init];
        delegate.destinationPath = [NSString stringWithUTF8String:config.destinationPath.c_str()];

        if (progressCallback) {
            delegate.progressCallback = ^(int64_t bytesWritten, int64_t totalBytes) {
                DownloadProgress progress;
                progress.bytesReceived = bytesWritten;
                progress.totalBytes = totalBytes;
                progress.progress = totalBytes > 0 ? static_cast<double>(bytesWritten) / totalBytes : 0.0;
                progressCallback(progress);
            };
        }

        NSURLSessionConfiguration* sessionConfig = [NSURLSessionConfiguration defaultSessionConfiguration];
        sessionConfig.timeoutIntervalForRequest = config.timeoutMs / 1000.0;
        sessionConfig.timeoutIntervalForResource = config.timeoutMs / 1000.0;
        NSURLSession* downloadSession = [NSURLSession sessionWithConfiguration:sessionConfig
                                                                      delegate:delegate
                                                                 delegateQueue:nil];

        NSURLSessionDownloadTask* task = [downloadSession downloadTaskWithRequest:request];
        [task resume];

        if (!waitForCompletion(task, downloadSession, delegate.semaphore, config.timeoutMs)) {
            result.errorMessage = "HTTP download exceeded its completion deadline";
            return result;
        }

        [downloadSession invalidateAndCancel];

        result.success = delegate.success;
        result.statusCode = static_cast<int32_t>([delegate statusCode]);
        result.filePath = config.destinationPath;
        result.fileSize = delegate.fileSize;

        if (delegate.error) {
            result.errorMessage = delegate.error.localizedDescription.UTF8String;
        }

        return result;
    }
}

// ============================================================================
// File Upload
// ============================================================================

NSURL* IOHttpClientIOS::createMultipartBody(const UploadConfig& config, NSString** boundary) {
    *boundary = [@"----IOHttpClientBoundary" stringByAppendingString:NSUUID.UUID.UUIDString];
    NSURL* temp = [NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString]];
    std::ofstream out(temp.path.UTF8String, std::ios::binary | std::ios::trunc);
    std::ifstream input(config.filePath, std::ios::binary);
    if (!out || !input) { [NSFileManager.defaultManager removeItemAtURL:temp error:nil]; return nil; }
    auto marker = std::string((*boundary).UTF8String);
    for (const auto& [key, value] : config.formFields) {
        out << "--" << marker << "\r\nContent-Disposition: form-data; name=\"" << key << "\"\r\n\r\n" << value << "\r\n";
    }
    auto name = config.fileName.empty() ? std::filesystem::path(config.filePath).filename().string() : config.fileName;
    out << "--" << marker << "\r\nContent-Disposition: form-data; name=\"" << config.fieldName << "\"; filename=\"" << name
        << "\"\r\nContent-Type: " << (config.mimeType.empty() ? "application/octet-stream" : config.mimeType) << "\r\n\r\n";
    // Stream the body to a temporary file; NSURLSession streams it from disk.
    // No NSData allocation proportional to the uploaded file size.
    std::array<char, 64 * 1024> chunk;
    while (input) {
        input.read(chunk.data(), chunk.size());
        out.write(chunk.data(), input.gcount());
    }
    out << "\r\n--" << marker << "--\r\n";
    out.close();
    if (input.bad() || !out) { [NSFileManager.defaultManager removeItemAtURL:temp error:nil]; return nil; }
    return temp;
}

UploadResult IOHttpClientIOS::upload(const UploadConfig& config,
                                      UploadProgressCallback progressCallback) {
    @autoreleasepool {
        UploadResult result;

        if (config.timeoutMs <= 0) {
            result.errorMessage = "timeoutMs must be greater than zero";
            return result;
        }

        NSFileManager* fileManager = [NSFileManager defaultManager];
        NSString* filePath = [NSString stringWithUTF8String:config.filePath.c_str()];
        if (![fileManager fileExistsAtPath:filePath]) {
            result.errorMessage = "File not found: " + config.filePath;
            return result;
        }

        NSURL* url = [NSURL URLWithString:[NSString stringWithUTF8String:config.url.c_str()]];
        if (!url || !([url.scheme.lowercaseString isEqualToString:@"http"] || [url.scheme.lowercaseString isEqualToString:@"https"])) {
            result.errorMessage = "Invalid URL: " + config.url;
            return result;
        }

        NSString* boundary = nil;
        NSURL* body = createMultipartBody(config, &boundary);
        if (!body) {
            result.errorMessage = "Failed to read file: " + config.filePath;
            return result;
        }

        NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:url];
        request.HTTPMethod = @"POST";
        request.timeoutInterval = config.timeoutMs / 1000.0;

        NSString* contentType = [NSString stringWithFormat:@"multipart/form-data; boundary=%@", boundary];
        [request setValue:contentType forHTTPHeaderField:@"Content-Type"];

        for (const auto& [key, value] : config.headers) {
            [request setValue:[NSString stringWithUTF8String:value.c_str()]
           forHTTPHeaderField:[NSString stringWithUTF8String:key.c_str()]];
        }


        IOSyncRequestDelegate* delegate = [[IOSyncRequestDelegate alloc] init];

        if (progressCallback) {
            delegate.progressCallback = ^(int64_t bytesSent, int64_t totalBytes) {
                UploadProgress progress;
                progress.bytesSent = bytesSent;
                progress.totalBytes = totalBytes;
                progress.progress = totalBytes > 0 ? static_cast<double>(bytesSent) / totalBytes : 0.0;
                progressCallback(progress);
            };
        }

        NSURLSessionConfiguration* sessionConfig = [NSURLSessionConfiguration defaultSessionConfiguration];
        sessionConfig.timeoutIntervalForRequest = config.timeoutMs / 1000.0;
        sessionConfig.timeoutIntervalForResource = config.timeoutMs / 1000.0;
        NSURLSession* uploadSession = [NSURLSession sessionWithConfiguration:sessionConfig
                                                                    delegate:delegate
                                                               delegateQueue:nil];

        NSURLSessionUploadTask* task = [uploadSession uploadTaskWithRequest:request fromFile:body];
        [task resume];

        if (!waitForCompletion(task, uploadSession, delegate.semaphore, config.timeoutMs)) {
            [NSFileManager.defaultManager removeItemAtURL:body error:nil];
            result.errorMessage = "HTTP upload exceeded its completion deadline";
            return result;
        }

        [uploadSession invalidateAndCancel];
        [NSFileManager.defaultManager removeItemAtURL:body error:nil];

        NSError* error = [delegate error];
        if (error) {
            result.success = false;
            result.errorMessage = [[error localizedDescription] UTF8String];
        } else {
            result.success = true;
            NSHTTPURLResponse* response = [delegate response];
            result.statusCode = static_cast<int32_t>([response statusCode]);

            NSMutableData* receivedData = [delegate receivedData];
            if ([receivedData length] > 0) {
                const uint8_t* bytes = static_cast<const uint8_t*>([receivedData bytes]);
                result.responseBody.assign(bytes, bytes + [receivedData length]);
            }
        }

        return result;
    }
}

// ============================================================================
// Factory Method (iOS)
// ============================================================================

std::shared_ptr<IOHttpClient> IOHttpClient::create() {
    return std::make_shared<IOHttpClientIOS>();
}

} // namespace rct_io::network

#endif // __APPLE__
