/**
 * @file LoggerIOS.mm
 * @brief iOS logging implementation
 *
 * Uses NSLog to output logs to Xcode console.
 *
 * Copyright (c) 2025 arcticfox
 */

#if defined(__APPLE__)

#include "Logger.h"
#import <Foundation/Foundation.h>

namespace rct_io {

void Logger::platformLog(LogLevel level, const std::string& tag, const std::string& message) {
    // Convert level to string prefix
    const char* levelStr;
    switch (level) {
        case LogLevel::Verbose:
            levelStr = "V";
            break;
        case LogLevel::Debug:
            levelStr = "D";
            break;
        case LogLevel::Info:
            levelStr = "I";
            break;
        case LogLevel::Warn:
            levelStr = "W";
            break;
        case LogLevel::Error:
            levelStr = "E";
            break;
        default:
            levelStr = "D";
            break;
    }

    // Convert to NSString and use NSLog
    NSString* tagStr = [[NSString alloc] initWithBytes:tag.data()
                                                length:tag.size()
                                              encoding:NSUTF8StringEncoding];
    NSString* msgStr = [[NSString alloc] initWithBytes:message.data()
                                                length:message.size()
                                              encoding:NSUTF8StringEncoding];

    NSLog(@"[%s/%@] %@", levelStr, tagStr, msgStr);
}

} // namespace rct_io

#endif // __APPLE__
