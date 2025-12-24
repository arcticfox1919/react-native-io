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
 * Uses C FILE* for maximum performance and portability.
 * Supports both text and binary operations.
 *
 * @note This class is NOT thread-safe. Each thread should have its own handle.
 */
class IOFileHandle {
private:
    FILE* file_ = nullptr;
    std::string path_;
    FileOpenMode mode_;
    int64_t size_ = -1;  // Cached file size, -1 = unknown

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
        file_ = std::fopen(path.c_str(), getModeString(mode));
        if (!file_) {
            throw std::runtime_error("Cannot open file: " + path);
        }
    }

    ~IOFileHandle() {
        close();
    }

    // Non-copyable
    IOFileHandle(const IOFileHandle&) = delete;
    IOFileHandle& operator=(const IOFileHandle&) = delete;

    // Movable
    IOFileHandle(IOFileHandle&& other) noexcept
        : file_(other.file_)
        , path_(std::move(other.path_))
        , mode_(other.mode_)
        , size_(other.size_)
    {
        other.file_ = nullptr;
    }

    IOFileHandle& operator=(IOFileHandle&& other) noexcept {
        if (this != &other) {
            close();
            file_ = other.file_;
            path_ = std::move(other.path_);
            mode_ = other.mode_;
            size_ = other.size_;
            other.file_ = nullptr;
        }
        return *this;
    }

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
        return file_ != nullptr;
    }

    /**
     * @brief Get file size
     * @note Caches the result after first call
     */
    [[nodiscard]] auto getSize() -> int64_t {
        ensureOpen();

        if (size_ < 0) {
            // Save current position
            auto currentPos = std::ftell(file_);
            // Seek to end
            std::fseek(file_, 0, SEEK_END);
            size_ = std::ftell(file_);
            // Restore position
            std::fseek(file_, currentPos, SEEK_SET);
        }
        return size_;
    }

    /**
     * @brief Get current file position
     */
    [[nodiscard]] auto getPosition() const -> int64_t {
        ensureOpen();
        return std::ftell(file_);
    }

    /**
     * @brief Check if at end of file
     * @note Uses position comparison instead of feof() for accurate detection
     *       after reading all content (feof only triggers after a failed read)
     */
    [[nodiscard]] auto isEOF() -> bool {
        ensureOpen();
        // feof() only returns true after attempting to read past EOF
        // So we compare current position with file size for accurate detection
        auto currentPos = std::ftell(file_);
        auto fileSize = getSize();
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
        ensureOpen();

        int whence;
        switch (origin) {
            case SeekOrigin::Begin:   whence = SEEK_SET; break;
            case SeekOrigin::Current: whence = SEEK_CUR; break;
            case SeekOrigin::End:     whence = SEEK_END; break;
            default: whence = SEEK_SET;
        }

        if (std::fseek(file_, static_cast<long>(offset), whence) != 0) {
            throw std::runtime_error("Seek failed");
        }

        return std::ftell(file_);
    }

    /**
     * @brief Rewind to beginning of file
     */
    auto rewind() -> void {
        ensureOpen();
        std::rewind(file_);
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
        ensureOpen();
        if (!canRead()) {
            throw std::runtime_error("File not opened for reading");
        }

        if (size < 0) {
            // Read all remaining
            auto currentPos = std::ftell(file_);
            std::fseek(file_, 0, SEEK_END);
            auto endPos = std::ftell(file_);
            std::fseek(file_, currentPos, SEEK_SET);
            size = endPos - currentPos;
        }

        std::vector<uint8_t> buffer(static_cast<size_t>(size));
        auto bytesRead = std::fread(buffer.data(), 1, buffer.size(), file_);

        if (bytesRead < buffer.size() && std::ferror(file_)) {
            throw std::runtime_error("Read error");
        }

        buffer.resize(bytesRead);
        return buffer;
    }

    /**
     * @brief Read file as string
     * @param size Number of bytes to read, -1 for all remaining
     * @return String content
     */
    [[nodiscard]] auto readString(int64_t size = -1) -> std::string {
        auto bytes = read(size);
        return std::string(bytes.begin(), bytes.end());
    }

    /**
     * @brief Read a single line (up to newline or EOF)
     * @param maxLength Maximum line length (default 64KB)
     * @return Line content (without newline), empty string at EOF
     */
    [[nodiscard]] auto readLine(size_t maxLength = 65536) -> std::string {
        ensureOpen();
        if (!canRead()) {
            throw std::runtime_error("File not opened for reading");
        }

        std::string line;
        line.reserve(256);

        int ch;
        while ((ch = std::fgetc(file_)) != EOF && line.size() < maxLength) {
            if (ch == '\n') {
                break;
            }
            if (ch != '\r') {  // Skip CR in CRLF
                line.push_back(static_cast<char>(ch));
            }
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
        ensureOpen();
        if (!canWrite()) {
            throw std::runtime_error("File not opened for writing");
        }

        auto written = std::fwrite(data.data(), 1, data.size(), file_);
        if (written < data.size()) {
            throw std::runtime_error("Write error");
        }

        // Invalidate cached size
        size_ = -1;

        return written;
    }

    /**
     * @brief Write string to file
     * @param content String to write
     * @return Number of bytes written
     */
    auto writeString(const std::string& content) -> size_t {
        ensureOpen();
        if (!canWrite()) {
            throw std::runtime_error("File not opened for writing");
        }

        auto written = std::fwrite(content.data(), 1, content.size(), file_);
        if (written < content.size()) {
            throw std::runtime_error("Write error");
        }

        // Invalidate cached size
        size_ = -1;

        return written;
    }

    /**
     * @brief Write a line (appends newline)
     * @param line Line content
     * @return Number of bytes written (including newline)
     */
    auto writeLine(const std::string& line) -> size_t {
        auto written = writeString(line);
        written += writeString("\n");
        return written;
    }

    // ========================================================================
    // Control Operations
    // ========================================================================

    /**
     * @brief Flush buffered data to disk
     */
    auto flush() -> void {
        ensureOpen();
        if (std::fflush(file_) != 0) {
            throw std::runtime_error("Flush failed");
        }
    }

    /**
     * @brief Truncate file at current position
     * @note Platform-specific, may not work on all systems
     */
    auto truncate() -> void {
        ensureOpen();
        if (!canWrite()) {
            throw std::runtime_error("File not opened for writing");
        }

        flush();
        auto pos = std::ftell(file_);

#ifdef _WIN32
        if (_chsize_s(_fileno(file_), pos) != 0) {
            throw std::runtime_error("Truncate failed");
        }
#else
        if (ftruncate(fileno(file_), pos) != 0) {
            throw std::runtime_error("Truncate failed");
        }
#endif

        // Invalidate cached size
        size_ = -1;
    }

    /**
     * @brief Close the file handle
     */
    auto close() -> void {
        if (file_) {
            std::fclose(file_);
            file_ = nullptr;
        }
    }
};

} // namespace rct_io

#endif // IO_FILE_HANDLE_HPP
