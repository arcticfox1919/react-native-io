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

// ============================================================================
// Sync Request Delegate
// ============================================================================

@interface IOSyncRequestDelegate : NSObject <NSURLSessionDataDelegate>
@property (nonatomic, strong) NSMutableData* receivedData;
@property (nonatomic, strong) NSHTTPURLResponse* response;
@property (nonatomic, strong) NSError* error;
@property (nonatomic, assign) BOOL completed;
@property (nonatomic, strong) dispatch_semaphore_t semaphore;
@property (nonatomic, copy) void (^progressCallback)(int64_t, int64_t);
@end

@implementation IOSyncRequestDelegate

- (instancetype)init {
    if (self = [super init]) {
        _receivedData = [NSMutableData data];
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

- (void)URLSession:(NSURLSession *)session
          dataTask:(NSURLSessionDataTask *)dataTask
    didReceiveData:(NSData *)data {
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
    self.error = error;
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

    NSError* moveError = nil;
    NSFileManager* fileManager = [NSFileManager defaultManager];

    NSURL* destURL = [NSURL fileURLWithPath:self.destinationPath];
    if ([fileManager fileExistsAtPath:self.destinationPath]) {
        [fileManager removeItemAtPath:self.destinationPath error:nil];
    }

    NSString* directory = [self.destinationPath stringByDeletingLastPathComponent];
    [fileManager createDirectoryAtPath:directory
           withIntermediateDirectories:YES
                            attributes:nil
                                 error:nil];

    if ([fileManager moveItemAtURL:location toURL:destURL error:&moveError]) {
        NSDictionary* attrs = [fileManager attributesOfItemAtPath:self.destinationPath error:nil];
        self.fileSize = [attrs fileSize];
        self.success = YES;
    } else {
        self.error = moveError;
        self.success = NO;
    }
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

// ============================================================================
// IOHttpClientIOS Implementation
// ============================================================================

namespace rct_io::network {

IOHttpClientIOS::IOHttpClientIOS() {
    @autoreleasepool {
        NSURLSessionConfiguration* config = [NSURLSessionConfiguration defaultSessionConfiguration];
        config.timeoutIntervalForRequest = 30.0;
        config.timeoutIntervalForResource = 60.0;

        delegateQueue_ = [[NSOperationQueue alloc] init];
        delegateQueue_.maxConcurrentOperationCount = 4;

        session_ = [NSURLSession sessionWithConfiguration:config
                                                 delegate:nil
                                            delegateQueue:delegateQueue_];
    }
}

IOHttpClientIOS::~IOHttpClientIOS() {
    @autoreleasepool {
        [session_ invalidateAndCancel];
    }
}

NSMutableURLRequest* IOHttpClientIOS::createRequest(const HttpRequestConfig& config) {
    @autoreleasepool {
        NSURL* url = [NSURL URLWithString:[NSString stringWithUTF8String:config.url.c_str()]];
        if (!url) {
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
            auto bodyBytes = config.getBodyBytes();
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
            NSString* value = headers[key];
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
        NSMutableURLRequest* request = createRequest(config);
        if (!request) {
            HttpResponse response;
            response.errorMessage = "Invalid URL: " + config.url;
            return response;
        }

        IOSyncRequestDelegate* delegate = [[IOSyncRequestDelegate alloc] init];

        NSURLSessionConfiguration* sessionConfig = [NSURLSessionConfiguration defaultSessionConfiguration];
        sessionConfig.timeoutIntervalForRequest = config.timeoutMs / 1000.0;

        NSURLSession* session = [NSURLSession sessionWithConfiguration:sessionConfig
                                                              delegate:delegate
                                                         delegateQueue:nil];

        NSURLSessionDataTask* task = [session dataTaskWithRequest:request];
        [task resume];

        dispatch_semaphore_wait(delegate.semaphore, DISPATCH_TIME_FOREVER);

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

        NSURL* url = [NSURL URLWithString:[NSString stringWithUTF8String:config.url.c_str()]];
        if (!url) {
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
            NSFileManager* fileManager = [NSFileManager defaultManager];
            NSString* destPath = [NSString stringWithUTF8String:config.destinationPath.c_str()];
            if ([fileManager fileExistsAtPath:destPath]) {
                NSDictionary* attrs = [fileManager attributesOfItemAtPath:destPath error:nil];
                int64_t existingSize = [attrs fileSize];
                NSString* rangeHeader = [NSString stringWithFormat:@"bytes=%lld-", existingSize];
                [request setValue:rangeHeader forHTTPHeaderField:@"Range"];
            }
        }

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
        NSURLSession* downloadSession = [NSURLSession sessionWithConfiguration:sessionConfig
                                                                      delegate:delegate
                                                                 delegateQueue:nil];

        NSURLSessionDownloadTask* task = [downloadSession downloadTaskWithRequest:request];
        [task resume];

        dispatch_semaphore_wait(delegate.semaphore, DISPATCH_TIME_FOREVER);

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

NSData* IOHttpClientIOS::createMultipartBody(const UploadConfig& config, NSString** boundary) {
    @autoreleasepool {
        *boundary = [NSString stringWithFormat:@"----IOHttpClientBoundary%lld",
                     (long long)[[NSDate date] timeIntervalSince1970] * 1000];

        NSMutableData* body = [NSMutableData data];

        for (const auto& [key, value] : config.formFields) {
            [body appendData:[[NSString stringWithFormat:@"--%@\r\n", *boundary]
                             dataUsingEncoding:NSUTF8StringEncoding]];
            [body appendData:[[NSString stringWithFormat:@"Content-Disposition: form-data; name=\"%s\"\r\n\r\n",
                              key.c_str()] dataUsingEncoding:NSUTF8StringEncoding]];
            [body appendData:[[NSString stringWithFormat:@"%s\r\n", value.c_str()]
                             dataUsingEncoding:NSUTF8StringEncoding]];
        }

        NSString* filePath = [NSString stringWithUTF8String:config.filePath.c_str()];
        NSData* fileData = [NSData dataWithContentsOfFile:filePath];

        if (!fileData) {
            return nil;
        }

        NSString* fileName = config.fileName.empty() ?
            [filePath lastPathComponent] :
            [NSString stringWithUTF8String:config.fileName.c_str()];

        NSString* mimeType = config.mimeType.empty() ?
            @"application/octet-stream" :
            [NSString stringWithUTF8String:config.mimeType.c_str()];

        NSString* fieldName = [NSString stringWithUTF8String:config.fieldName.c_str()];

        [body appendData:[[NSString stringWithFormat:@"--%@\r\n", *boundary]
                         dataUsingEncoding:NSUTF8StringEncoding]];
        [body appendData:[[NSString stringWithFormat:@"Content-Disposition: form-data; name=\"%@\"; filename=\"%@\"\r\n",
                          fieldName, fileName] dataUsingEncoding:NSUTF8StringEncoding]];
        [body appendData:[[NSString stringWithFormat:@"Content-Type: %@\r\n\r\n", mimeType]
                         dataUsingEncoding:NSUTF8StringEncoding]];
        [body appendData:fileData];
        [body appendData:[@"\r\n" dataUsingEncoding:NSUTF8StringEncoding]];

        [body appendData:[[NSString stringWithFormat:@"--%@--\r\n", *boundary]
                         dataUsingEncoding:NSUTF8StringEncoding]];

        return body;
    }
}

UploadResult IOHttpClientIOS::upload(const UploadConfig& config,
                                      UploadProgressCallback progressCallback) {
    @autoreleasepool {
        UploadResult result;

        NSFileManager* fileManager = [NSFileManager defaultManager];
        NSString* filePath = [NSString stringWithUTF8String:config.filePath.c_str()];
        if (![fileManager fileExistsAtPath:filePath]) {
            result.errorMessage = "File not found: " + config.filePath;
            return result;
        }

        NSURL* url = [NSURL URLWithString:[NSString stringWithUTF8String:config.url.c_str()]];
        if (!url) {
            result.errorMessage = "Invalid URL: " + config.url;
            return result;
        }

        NSString* boundary = nil;
        NSData* body = createMultipartBody(config, &boundary);
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

        request.HTTPBody = body;

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
        NSURLSession* uploadSession = [NSURLSession sessionWithConfiguration:sessionConfig
                                                                    delegate:delegate
                                                               delegateQueue:nil];

        NSURLSessionDataTask* task = [uploadSession dataTaskWithRequest:request];
        [task resume];

        dispatch_semaphore_wait([delegate semaphore], DISPATCH_TIME_FOREVER);

        [uploadSession invalidateAndCancel];

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
