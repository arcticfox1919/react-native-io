/**
 * @file Logger.h
 * @brief Cross-platform logging utility using snprintf
 *
 * Provides a unified logging interface for iOS and Android platforms.
 * Uses NSLog on iOS and __android_log_print on Android.
 *
 * @example
 * ```cpp
 * Logger::d("MyTag", "User %s logged in at %d", username.c_str(), timestamp);
 * Logger::e("Network", "Request failed with status: %d", statusCode);
 * ```
 *
 * Copyright (c) 2025 arcticfox
 */

#pragma once

#include <cstdio>
#include <memory>
#include <string>

namespace rct_io {

// ============================================================================
// Log Levels
// ============================================================================

enum class LogLevel {
    Verbose = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
};

// ============================================================================
// Logger Class
// ============================================================================

class Logger {
private:
    Logger() = delete;

    // Platform-specific implementation (defined in .cpp/.mm files)
    static void platformLog(LogLevel level, const std::string& tag, const std::string& message);

    /**
     * Format string using snprintf (printf-style)
     */
    template <typename... Args>
    static std::string stringFormat(const std::string& format, Args... args) {
        int size_s = std::snprintf(nullptr, 0, format.c_str(), args...) + 1;
        if (size_s <= 0) {
            return format;
        }
        auto size = static_cast<size_t>(size_s);
        std::unique_ptr<char[]> buf(new char[size]);
        std::snprintf(buf.get(), size, format.c_str(), args...);
        return std::string(buf.get(), buf.get() + size - 1);
    }

public:
    // ========================================================================
    // Core Log Methods
    // ========================================================================

    static void log(LogLevel level, const std::string& tag, const std::string& message) {
        platformLog(level, tag, message);
    }

    template <typename... Args>
    static void log(LogLevel level, const std::string& tag, const std::string& format, Args&&... args) {
        std::string message = stringFormat(format, std::forward<Args>(args)...);
        platformLog(level, tag, message);
    }

    // ========================================================================
    // Convenience Methods with Tag
    // ========================================================================

    static void v(const std::string& tag, const std::string& message) {
        log(LogLevel::Verbose, tag, message);
    }

    template <typename... Args>
    static void v(const std::string& tag, const std::string& format, Args&&... args) {
        log(LogLevel::Verbose, tag, format, std::forward<Args>(args)...);
    }

    static void d(const std::string& tag, const std::string& message) {
        log(LogLevel::Debug, tag, message);
    }

    template <typename... Args>
    static void d(const std::string& tag, const std::string& format, Args&&... args) {
        log(LogLevel::Debug, tag, format, std::forward<Args>(args)...);
    }

    static void i(const std::string& tag, const std::string& message) {
        log(LogLevel::Info, tag, message);
    }

    template <typename... Args>
    static void i(const std::string& tag, const std::string& format, Args&&... args) {
        log(LogLevel::Info, tag, format, std::forward<Args>(args)...);
    }

    static void w(const std::string& tag, const std::string& message) {
        log(LogLevel::Warn, tag, message);
    }

    template <typename... Args>
    static void w(const std::string& tag, const std::string& format, Args&&... args) {
        log(LogLevel::Warn, tag, format, std::forward<Args>(args)...);
    }

    static void e(const std::string& tag, const std::string& message) {
        log(LogLevel::Error, tag, message);
    }

    template <typename... Args>
    static void e(const std::string& tag, const std::string& format, Args&&... args) {
        log(LogLevel::Error, tag, format, std::forward<Args>(args)...);
    }
};

} // namespace rct_io
