/**
 * @file LoggerAndroid.cpp
 * @brief Android NDK logging implementation
 *
 * Uses __android_log_print to output logs to logcat.
 *
 * Copyright (c) 2025 arcticfox
 */

#ifdef __ANDROID__

#include "Logger.h"
#include <android/log.h>

namespace rct_io {

void Logger::platformLog(LogLevel level, const std::string& tag, const std::string& message) {
    android_LogPriority priority;

    switch (level) {
        case LogLevel::Verbose:
            priority = ANDROID_LOG_VERBOSE;
            break;
        case LogLevel::Debug:
            priority = ANDROID_LOG_DEBUG;
            break;
        case LogLevel::Info:
            priority = ANDROID_LOG_INFO;
            break;
        case LogLevel::Warn:
            priority = ANDROID_LOG_WARN;
            break;
        case LogLevel::Error:
            priority = ANDROID_LOG_ERROR;
            break;
        default:
            priority = ANDROID_LOG_DEBUG;
            break;
    }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-security"
    __android_log_print(priority, tag.c_str(), message.c_str());
#pragma clang diagnostic pop
}

} // namespace rct_io

#endif // __ANDROID__
