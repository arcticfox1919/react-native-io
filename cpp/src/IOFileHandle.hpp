/**
 * @file IOFileHandle.hpp
 * @brief File handle for streaming file operations
 *
 * Provides low-level file access with persistent file handle,
 * supporting read, write, seek operations without reopening file.
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

#ifndef IO_FILE_HANDLE_HPP
#define IO_FILE_HANDLE_HPP

#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <filesystem>
#include <cstdio>
#include <mutex>
#include <algorithm>
#include <sys/stat.h>
#include "IOLimits.hpp"

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace rct_io {

/**
 * @brief File open mode
 */
enum class FileOpenMode : int {
    Read = 0,       // 'r'  - read only, file must exist
    Write = 1,      // 'w'  - write only, truncate/create
    Append = 2,     // 'a'  - append only, create if not exist
    ReadWrite = 3,  // 'r+' - read/write, file must exist
    WriteRead = 4,  // 'w+' - read/write, truncate/create
    AppendRead = 5, // 'a+' - read/append, create if not exist
};

/**
 * @brief Seek origin for position operations
 */
enum class SeekOrigin : int {
    Begin = 0,   // SEEK_SET
    Current = 1, // SEEK_CUR
    End = 2,     // SEEK_END
};

/**
 * @brief Low-level file handle for streaming operations
 *
 * Uses buffered C FILE* for file-handle operations.
 * Supports both text and binary operations.
 *
 * @note Operations and close are serialized per handle. Concurrent operations
 * are safe, but callers must await dependent seek/read/write operations for ordering.
 */
class IOFileHandle {
private:
    mutable std::mutex mutex_;
    FILE* file_ = nullptr;
    std::string path_;
    FileOpenMode mode_;
    bool lastWasRead_ = false;
    bool lastWasWrite_ = false;
    void prepareRead() {
        if (lastWasWrite_ && std::fflush(file_) != 0) throw std::runtime_error("Flush before read failed");
        lastWasWrite_ = false;
        lastWasRead_ = true;
    }
    void prepareWrite() {
        if (lastWasRead_ && ::fseeko(file_, 0, SEEK_CUR) != 0) throw std::runtime_error("Position before write failed");
        lastWasRead_ = false;
        lastWasWrite_ = true;
    }

    [[nodiscard]] int64_t getSizeUnlocked() {
        ensureOpen();
        if (lastWasWrite_ && std::fflush(file_) != 0) throw std::runtime_error("Flush failed");
        struct stat status;
        if (::fstat(fileno(file_), &status) != 0) throw std::runtime_error("Cannot get file size");
        return static_cast<int64_t>(status.st_size);
    }

    [[nodiscard]] int64_t getPositionUnlocked() const {
        ensureOpen();
        auto position = ::ftello(file_);
        if (position < 0) throw std::runtime_error("Cannot determine file position");
        return position;
    }

    [[nodiscard]] std::vector<uint8_t> readUnlocked(int64_t size) {
        ensureOpen();
        if (!canRead()) throw std::runtime_error("File not opened for reading");
        if (size < -1) throw std::invalid_argument("Read size must be -1 or nonnegative");
        prepareRead();
        auto currentPos = getPositionUnlocked();
        auto remaining = std::max<int64_t>(0, getSizeUnlocked() - currentPos);
        size = size == -1 ? remaining : std::min(size, remaining);
        checkBufferSize(static_cast<size_t>(size));
        if (size == 0) return {};
        std::vector<uint8_t> buffer(static_cast<size_t>(size));
        auto bytesRead = std::fread(buffer.data(), 1, buffer.size(), file_);
        if (bytesRead < buffer.size() && std::ferror(file_)) throw std::runtime_error("Read error");
        buffer.resize(bytesRead);
        return buffer;
    }

    size_t writeStringUnlocked(const std::string& content) {
        ensureOpen();
        if (!canWrite()) throw std::runtime_error("File not opened for writing");
        checkBufferSize(content.size());
        prepareWrite();
        auto written = std::fwrite(content.data(), 1, content.size(), file_);
        if (written < content.size()) throw std::runtime_error("Write error");
        return written;
    }

    void flushUnlocked() {
        ensureOpen();
        if (std::fflush(file_) != 0) throw std::runtime_error("Flush failed");
    }

    /**
     * @brief Convert FileOpenMode to fopen mode string
     */
    [[nodiscard]] static auto getModeString(FileOpenMode mode) -> const char* {
        switch (mode) {
            case FileOpenMode::Read:      return "rb";
            case FileOpenMode::Write:     return "wb";
            case FileOpenMode::Append:    return "ab";
            case FileOpenMode::ReadWrite: return "r+b";
            case FileOpenMode::WriteRead: return "w+b";
            case FileOpenMode::AppendRead: return "a+b";
            default: return "rb";
        }
    }

    /**
     * @brief Check if mode allows reading
     */
    [[nodiscard]] auto canRead() const -> bool {
        return mode_ == FileOpenMode::Read ||
               mode_ == FileOpenMode::ReadWrite ||
               mode_ == FileOpenMode::WriteRead ||
               mode_ == FileOpenMode::AppendRead;
    }

    /**
     * @brief Check if mode allows writing
     */
    [[nodiscard]] auto canWrite() const -> bool {
        return mode_ == FileOpenMode::Write ||
               mode_ == FileOpenMode::Append ||
               mode_ == FileOpenMode::ReadWrite ||
               mode_ == FileOpenMode::WriteRead ||
               mode_ == FileOpenMode::AppendRead;
    }

    /**
     * @brief Ensure file is open
     */
    auto ensureOpen() const -> void {
        if (!file_) {
            throw std::runtime_error("File handle is closed");
        }
    }

public:
    IOFileHandle() = default;

    /**
     * @brief Open a file with specified mode
     * @param path File path
     * @param mode Open mode
     * @throws std::runtime_error if file cannot be opened
     */
    IOFileHandle(const std::string& path, FileOpenMode mode)
        : path_(path), mode_(mode)
    {
        checkPath(path);
        if (static_cast<int>(mode) < 0 || static_cast<int>(mode) > 5) throw std::invalid_argument("Invalid file open mode");
        file_ = std::fopen(path.c_str(), getModeString(mode));
        if (!file_) {
            throw std::runtime_error("Cannot open file: " + path);
        }
    }

    ~IOFileHandle() noexcept { try { close(); } catch (...) {} }

    // Non-copyable
    IOFileHandle(const IOFileHandle&) = delete;
    IOFileHandle& operator=(const IOFileHandle&) = delete;

    // Movable
    IOFileHandle(IOFileHandle&& other) = delete;
    IOFileHandle& operator=(IOFileHandle&& other) = delete;

    // ========================================================================
    // Properties
    // ========================================================================

    /**
     * @brief Get file path
     */
    [[nodiscard]] auto getPath() const -> const std::string& {
        return path_;
    }

    /**
     * @brief Check if file is open
     */
    [[nodiscard]] auto isOpen() const -> bool {
        std::lock_guard<std::mutex> lock(mutex_);
        return file_ != nullptr;
    }

    /**
     * @brief Get file size
     * @note Queries the open file descriptor on every call
     */
    [[nodiscard]] auto getSize() -> int64_t {
        std::lock_guard<std::mutex> lock(mutex_);
        return getSizeUnlocked();
    }

    /**
     * @brief Get current file position
     */
    [[nodiscard]] auto getPosition() const -> int64_t {
        std::lock_guard<std::mutex> lock(mutex_);
        return getPositionUnlocked();
    }

    /**
     * @brief Check if at end of file
     * @note Uses position comparison instead of feof() for accurate detection
     *       after reading all content (feof only triggers after a failed read)
     */
    [[nodiscard]] auto isEOF() -> bool {
        std::lock_guard<std::mutex> lock(mutex_);
        ensureOpen();
        // feof() only returns true after attempting to read past EOF
        // So we compare current position with file size for accurate detection
        auto currentPos = getPositionUnlocked();
        auto fileSize = getSizeUnlocked();
        return currentPos >= fileSize;
    }

    // ========================================================================
    // Position Operations
    // ========================================================================

    /**
     * @brief Seek to position
     * @param offset Offset from origin
     * @param origin Seek origin (Begin, Current, End)
     * @return New position
     */
    auto seek(int64_t offset, SeekOrigin origin = SeekOrigin::Begin) -> int64_t {
        std::lock_guard<std::mutex> lock(mutex_);
        ensureOpen();

        int whence;
        switch (origin) {
            case SeekOrigin::Begin:   whence = SEEK_SET; break;
            case SeekOrigin::Current: whence = SEEK_CUR; break;
            case SeekOrigin::End:     whence = SEEK_END; break;
            default: throw std::invalid_argument("Invalid seek origin");
        }

        if (::fseeko(file_, static_cast<off_t>(offset), whence) != 0) {
            throw std::runtime_error("Seek failed");
        }

        lastWasRead_ = lastWasWrite_ = false;
        auto position = ::ftello(file_);
        if (position < 0) throw std::runtime_error("Cannot determine file position");
        return position;
    }

    /**
     * @brief Rewind to beginning of file
     */
    auto rewind() -> void {
        std::lock_guard<std::mutex> lock(mutex_);
        ensureOpen();
        std::rewind(file_);
        lastWasRead_ = lastWasWrite_ = false;
    }

    // ========================================================================
    // Read Operations
    // ========================================================================

    /**
     * @brief Read bytes from file
     * @param size Number of bytes to read, -1 for all remaining
     * @return Bytes read
     */
    [[nodiscard]] auto read(int64_t size = -1) -> std::vector<uint8_t> {
        std::lock_guard<std::mutex> lock(mutex_);
        return readUnlocked(size);
    }

    /**
     * @brief Read file as string
     * @param size Number of bytes to read, -1 for all remaining
     * @return String content
     */
    [[nodiscard]] auto readString(int64_t size = -1) -> std::string {
        std::lock_guard<std::mutex> lock(mutex_);
        auto bytes = readUnlocked(size);
        return std::string(bytes.begin(), bytes.end());
    }

    /**
     * @brief Read a single line (up to newline or EOF)
     * @param maxLength Maximum line length (default 64KB)
     * @return Line content (without newline), empty string at EOF
     */
    [[nodiscard]] auto readLine(size_t maxLength = 65536) -> std::string {
        std::lock_guard<std::mutex> lock(mutex_);
        ensureOpen();
        if (!canRead()) {
            throw std::runtime_error("File not opened for reading");
        }

        checkBufferSize(maxLength);
        prepareRead();
        std::string line;
        line.reserve(std::min<size_t>(maxLength, 256));
        while (line.size() < maxLength) {
            int ch = getc_unlocked(file_);
            if (ch == EOF) {
                if (std::ferror(file_)) throw std::runtime_error("Read line failed");
                break;
            }
            if (ch == '\n') {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                break;
            }
            line.push_back(static_cast<char>(ch));
        }

        return line;
    }

    // ========================================================================
    // Write Operations
    // ========================================================================

    /**
     * @brief Write bytes to file
     * @param data Data to write
     * @return Number of bytes written
     */
    auto write(const std::vector<uint8_t>& data) -> size_t {
        std::lock_guard<std::mutex> lock(mutex_);
        ensureOpen();
        if (!canWrite()) {
            throw std::runtime_error("File not opened for writing");
        }

        checkBufferSize(data.size());
        prepareWrite();
        auto written = std::fwrite(data.data(), 1, data.size(), file_);
        if (written < data.size()) {
            throw std::runtime_error("Write error");
        }


        return written;
    }

    /**
     * @brief Write string to file
     * @param content String to write
     * @return Number of bytes written
     */
    auto writeString(const std::string& content) -> size_t {
        std::lock_guard<std::mutex> lock(mutex_);
        return writeStringUnlocked(content);
    }

    /**
     * @brief Write a line (appends newline)
     * @param line Line content
     * @return Number of bytes written (including newline)
     */
    auto writeLine(const std::string& line) -> size_t {
        std::lock_guard<std::mutex> lock(mutex_);
        auto written = writeStringUnlocked(line);
        written += writeStringUnlocked("\n");
        return written;
    }

    // ========================================================================
    // Control Operations
    // ========================================================================

    /**
     * @brief Flush buffered data to disk
     */
    auto flush() -> void {
        std::lock_guard<std::mutex> lock(mutex_);
        flushUnlocked();
    }

    /**
     * @brief Truncate file at current position
     * @note Platform-specific, may not work on all systems
     */
    auto truncate() -> void {
        std::lock_guard<std::mutex> lock(mutex_);
        ensureOpen();
        if (!canWrite()) {
            throw std::runtime_error("File not opened for writing");
        }

        flushUnlocked();
        auto pos = ::ftello(file_);

#ifdef _WIN32
        if (_chsize_s(_fileno(file_), pos) != 0) {
            throw std::runtime_error("Truncate failed");
        }
#else
        if (ftruncate(fileno(file_), pos) != 0) {
            throw std::runtime_error("Truncate failed");
        }
#endif

    }

    /**
     * @brief Close the file handle
     */
    auto close() -> void {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_) {
            auto file = file_;
            file_ = nullptr;
            if (std::fclose(file) != 0) throw std::runtime_error("Close failed (buffered write may have failed)");
        }
    }
};

} // namespace rct_io

#endif // IO_FILE_HANDLE_HPP
