/**
 * @file IOFileSystem.hpp
 * @brief A modern C++20 filesystem library for React Native
 *
 * Provides file and directory operations optimized for mobile platforms.
 * All async operations use a thread pool to avoid blocking the JS thread.
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

#ifndef IO_FILE_SYSTEM_HPP
#define IO_FILE_SYSTEM_HPP

#include <jsi/jsi.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <chrono>
#include <optional>
#include <span>
#include <cstdio>
#include <variant>
#include <array>
#include "IOLimits.hpp"
#include "AtomicFileWriter.hpp"

// Hash algorithms
#include "md5.h"
#include "sha1.h"
#include "sha256.h"
#include "sha3.h"
#include "keccak.h"
#include "crc32.h"

namespace rct_io {

namespace fs = std::filesystem;
using namespace facebook::jsi;

// ============================================================================
// Type Definitions
// ============================================================================

/**
 * @brief File system entity type
 */
enum class EntityType : int {
    NotFound = 0,
    File = 1,
    Directory = 2,
};

/**
 * @brief File write mode
 */
enum class WriteMode : int {
    Overwrite = 0,   // Truncate and write
    Append = 1,      // Append to existing
    Atomic = 2,      // Replace via a sibling staging file
    AtomicDurable = 3, // Atomic + file and parent-directory fsync
};

/**
 * @brief Hash algorithm for file integrity check
 */
enum class HashAlgorithm : int {
    MD5 = 0,
    SHA1 = 1,
    SHA256 = 2,      // Default
    SHA3_224 = 3,
    SHA3_256 = 4,
    SHA3_384 = 5,
    SHA3_512 = 6,
    Keccak224 = 7,
    Keccak256 = 8,
    Keccak384 = 9,
    Keccak512 = 10,
    CRC32 = 11,
};

/**
 * @brief File metadata structure
 */
struct FileMetadata {
    int64_t size{0};
    int64_t modifiedTime{0};  // milliseconds since epoch
    EntityType type{EntityType::NotFound};

    [[nodiscard]] auto toJSObject(Runtime& runtime) const -> Object {
        Object result(runtime);
        result.setProperty(runtime, "size", static_cast<double>(size));
        result.setProperty(runtime, "modifiedTime", static_cast<double>(modifiedTime));
        result.setProperty(runtime, "type", static_cast<int>(type));
        return result;
    }
};

/**
 * @brief Directory entry information
 */
struct DirectoryEntry {
    std::string path;
    std::string name;
    EntityType type{EntityType::NotFound};
    int64_t size{0};

    [[nodiscard]] auto toJSObject(Runtime& runtime) const -> Object {
        Object result(runtime);
        result.setProperty(runtime, "path", String::createFromUtf8(runtime, path));
        result.setProperty(runtime, "name", String::createFromUtf8(runtime, name));
        result.setProperty(runtime, "type", static_cast<int>(type));
        result.setProperty(runtime, "size", static_cast<double>(size));
        return result;
    }
};

// ============================================================================
// Utility Functions
// ============================================================================

namespace detail {

/**
 * @brief Convert file_time_type to milliseconds since epoch
 * @note Uses portable conversion for Android NDK compatibility (no clock_cast)
 */
[[nodiscard]] inline auto toMilliseconds(const fs::file_time_type& fileTime) -> int64_t {
    // Portable conversion without clock_cast (not available in Android NDK)
    const auto systemTime = fs::file_time_type::clock::to_sys(fileTime);
    return std::chrono::duration_cast<std::chrono::milliseconds>(systemTime.time_since_epoch()).count();
}

/**
 * @brief Get entity type from filesystem status
 */
[[nodiscard]] inline auto getEntityType(fs::file_status status) -> EntityType {
    if (fs::is_regular_file(status)) {
        return EntityType::File;
    }
    if (fs::is_directory(status)) {
        return EntityType::Directory;
    }
    return EntityType::NotFound;
}

/**
 * @brief Ensure parent directory exists
 */
inline auto ensureParentDirectory(const fs::path& path) -> void {
    if (auto parent = path.parent_path(); !parent.empty()) {
        std::error_code ec;
        fs::create_directories(parent, ec);
        if (ec) throw fs::filesystem_error("Cannot create parent directory", parent, ec);
    }
}

} // namespace detail

// ============================================================================
// IOFileSystem Class
// ============================================================================

/**
 * @brief Main filesystem operations class
 *
 * Provides all file and directory operations with both sync and async variants.
 * Designed for mobile platforms (iOS/Android).
 */
class IOFileSystem {
    // Read directly into the requested container; text reads do not need an
    // intermediate byte vector containing a second copy of the entire file.
    template<typename Contents>
    [[nodiscard]] static auto readContents(const std::string& path) -> Contents {
        checkPath(path);
        std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(path.c_str(), "rb"), &std::fclose);
        if (!file) throw std::runtime_error("Cannot open file for reading: " + path);
        if (::fseeko(file.get(), 0, SEEK_END) != 0) throw std::runtime_error("Cannot seek file: " + path);
        auto end = ::ftello(file.get());
        if (end < 0) throw std::runtime_error("Cannot determine file size: " + path);
        checkBufferSize(static_cast<size_t>(end));
        if (::fseeko(file.get(), 0, SEEK_SET) != 0) throw std::runtime_error("Cannot seek file: " + path);
        Contents buffer(static_cast<size_t>(end), 0);
        const auto bytesRead = buffer.empty() ? 0 : std::fread(buffer.data(), 1, buffer.size(), file.get());
        if (std::ferror(file.get())) throw std::runtime_error("Error reading file: " + path);
        // A concurrent truncation must not turn the missing tail into zero bytes.
        buffer.resize(bytesRead);
        return buffer;
    }

public:
    IOFileSystem() = default;
    ~IOFileSystem() = default;

    // Non-copyable, non-movable (shared ownership via shared_ptr)
    IOFileSystem(const IOFileSystem&) = delete;
    IOFileSystem& operator=(const IOFileSystem&) = delete;
    IOFileSystem(IOFileSystem&&) = delete;
    IOFileSystem& operator=(IOFileSystem&&) = delete;

    // ========================================================================
    // File Query Operations
    // ========================================================================

    /**
     * @brief Check if a path exists
     */
    [[nodiscard]] auto exists(const std::string& path) const -> bool {
        std::error_code ec;
        return fs::exists(path, ec);
    }

    /**
     * @brief Check if path is a file
     */
    [[nodiscard]] auto isFile(const std::string& path) const -> bool {
        std::error_code ec;
        return fs::is_regular_file(path, ec);
    }

    /**
     * @brief Check if path is a directory
     */
    [[nodiscard]] auto isDirectory(const std::string& path) const -> bool {
        std::error_code ec;
        return fs::is_directory(path, ec);
    }

    /**
     * @brief Get file/directory metadata
     */
    [[nodiscard]] auto getMetadata(const std::string& path) const -> FileMetadata {
        FileMetadata metadata;
        std::error_code ec;

        auto status = fs::status(path, ec);
        if (ec || !fs::exists(status)) {
            return metadata;
        }

        metadata.type = detail::getEntityType(status);

        if (metadata.type == EntityType::File) {
            metadata.size = static_cast<int64_t>(fs::file_size(path, ec));
        }

        auto lastWrite = fs::last_write_time(path, ec);
        if (!ec) {
            metadata.modifiedTime = detail::toMilliseconds(lastWrite);
        }

        return metadata;
    }

    /**
     * @brief Get file size in bytes
     */
    [[nodiscard]] auto getFileSize(const std::string& path) const -> int64_t {
        std::error_code ec;
        auto size = fs::file_size(path, ec);
        if (ec) {
            throw std::runtime_error("Failed to get file size: " + path);
        }
        return static_cast<int64_t>(size);
    }

    /**
     * @brief Get last modified time (milliseconds since epoch)
     */
    [[nodiscard]] auto getModifiedTime(const std::string& path) const -> int64_t {
        std::error_code ec;
        auto time = fs::last_write_time(path, ec);
        if (ec) {
            throw std::runtime_error("Failed to get modified time: " + path);
        }
        return detail::toMilliseconds(time);
    }

    // ========================================================================
    // File Read Operations
    // ========================================================================

    /**
     * @brief Read entire file as UTF-8 string
     */
    [[nodiscard]] auto readString(const std::string& path) const -> std::string {
        return readContents<std::string>(path);
    }

    /**
     * @brief Read entire file as binary data
     */
    [[nodiscard]] auto readBytes(const std::string& path) const -> std::vector<uint8_t> {
        return readContents<std::vector<uint8_t>>(path);
    }

    // ========================================================================
    // File Write Operations
    // ========================================================================

    /**
     * @brief Write UTF-8 string to file
     * @param path File path
     * @param content Content to write
     * @param mode Write mode (Overwrite or Append)
     * @param createParents Create parent directories if needed
     */
    auto writeString(
        const std::string& path,
        const std::string& content,
        WriteMode mode = WriteMode::Overwrite,
        bool createParents = false
    ) -> void {
        checkBufferSize(content.size());
        checkPath(path);
        if (createParents) {
            detail::ensureParentDirectory(path);
        }

        if (mode == WriteMode::Atomic || mode == WriteMode::AtomicDurable) {
            detail::atomicWrite(path, reinterpret_cast<const uint8_t*>(content.data()), content.size(), mode == WriteMode::AtomicDurable);
            return;
        }
        if (mode != WriteMode::Overwrite && mode != WriteMode::Append) throw std::invalid_argument("Invalid write mode");

        auto openMode = std::ios::binary | std::ios::out;
        if (mode == WriteMode::Append) {
            openMode |= std::ios::app;
        } else {
            openMode |= std::ios::trunc;
        }

        std::ofstream file(path, openMode);
        if (!file) {
            throw std::runtime_error("Cannot open file for writing: " + path);
        }

        file.write(content.data(), static_cast<std::streamsize>(content.size()));

        if (!file) {
            throw std::runtime_error("Failed to write to file: " + path);
        }
        file.close();
        if (!file) throw std::runtime_error("Failed to close written file: " + path);
    }

    /**
     * @brief Write borrowed binary data to file. The view is consumed before return.
     */
    auto writeBytes(
        const std::string& path,
        std::span<const uint8_t> data,
        WriteMode mode = WriteMode::Overwrite,
        bool createParents = false
    ) -> void {
        checkBufferSize(data.size());
        checkPath(path);
        if (createParents) {
            detail::ensureParentDirectory(path);
        }

        if (mode == WriteMode::Atomic || mode == WriteMode::AtomicDurable) {
            detail::atomicWrite(path, data.data(), data.size(), mode == WriteMode::AtomicDurable);
            return;
        }
        if (mode != WriteMode::Overwrite && mode != WriteMode::Append) throw std::invalid_argument("Invalid write mode");

        auto openMode = std::ios::binary | std::ios::out;
        if (mode == WriteMode::Append) {
            openMode |= std::ios::app;
        } else {
            openMode |= std::ios::trunc;
        }

        std::ofstream file(path, openMode);
        if (!file) {
            throw std::runtime_error("Cannot open file for writing: " + path);
        }

        // Empty ArrayBuffers may expose a null data pointer. Opening the file
        // still applies overwrite/append semantics, without passing null to write.
        if (!data.empty()) {
            file.write(
                reinterpret_cast<const char*>(data.data()),
                static_cast<std::streamsize>(data.size())
            );
        }

        if (!file) {
            throw std::runtime_error("Failed to write bytes to file: " + path);
        }
        file.close();
        if (!file) throw std::runtime_error("Failed to close written file: " + path);
    }

    // ========================================================================
    // File Management Operations
    // ========================================================================

    /**
     * @brief Create an empty file
     * @param path File path
     * @param createParents Create parent directories if needed
     */
    auto createFile(const std::string& path, bool createParents = false) -> void {
        checkPath(path);
        if (createParents) {
            detail::ensureParentDirectory(path);
        }

        std::ofstream file(path, std::ios::app);
        if (!file) {
            throw std::runtime_error("Failed to create file: " + path);
        }
    }

    /**
     * @brief Delete a file
     * @returns true if file was deleted, false if it didn't exist
     */
    auto deleteFile(const std::string& path) -> bool {
        checkPath(path);
        std::error_code ec;
        bool removed = fs::remove(path, ec);
        if (ec) throw fs::filesystem_error("Cannot delete file", path, ec);
        return removed;
    }

    /**
     * @brief Copy a file
     * @param sourcePath Source file path
     * @param destinationPath Destination path
     * @param overwrite Overwrite if destination exists
     */
    auto copyFile(
        const std::string& sourcePath,
        const std::string& destinationPath,
        bool overwrite = true
    ) -> void {
        auto options = overwrite
            ? fs::copy_options::overwrite_existing
            : fs::copy_options::none;

        std::error_code ec;
        fs::copy_file(sourcePath, destinationPath, options, ec);

        if (ec) {
            throw std::runtime_error("Failed to copy file: " + ec.message());
        }
    }

    /**
     * @brief Move/rename a file
     */
    auto moveFile(const std::string& sourcePath, const std::string& destinationPath) -> void {
        std::error_code ec;
        fs::rename(sourcePath, destinationPath, ec);

        if (ec) {
            throw std::runtime_error("Failed to move file: " + ec.message());
        }
    }

    // ========================================================================
    // Directory Operations
    // ========================================================================

    /**
     * @brief Create a directory
     * @param path Directory path
     * @param recursive Create parent directories if needed
     */
    auto createDirectory(const std::string& path, bool recursive = false) -> void {
        std::error_code ec;

        bool created = recursive
            ? fs::create_directories(path, ec)
            : fs::create_directory(path, ec);

        if (ec) {
            throw std::runtime_error("Failed to create directory: " + ec.message());
        }

        // create_directories returns false if directory already exists, which is fine
        (void)created;
    }

    /**
     * @brief Delete a directory
     * @param path Directory path
     * @param recursive Delete contents recursively
     * @returns Number of items deleted
     */
    auto deleteDirectory(const std::string& path, bool recursive = false) -> int64_t {
        std::error_code ec;
        int64_t count = 0;

        if (recursive) {
            count = static_cast<int64_t>(fs::remove_all(path, ec));
        } else {
            count = fs::remove(path, ec) ? 1 : 0;
        }

        if (ec) {
            throw std::runtime_error("Failed to delete directory: " + ec.message());
        }

        return count;
    }

    /**
     * @brief List directory contents
     * @param path Directory path
     * @param recursive List recursively
     * @returns Vector of directory entries
     */
    [[nodiscard]] auto listDirectory(
        const std::string& path,
        bool recursive = false
    ) const -> std::vector<DirectoryEntry> {
        std::vector<DirectoryEntry> entries;
        std::error_code ec;

        auto processEntry = [&entries](const fs::directory_entry& entry) {
            DirectoryEntry dirEntry;
            dirEntry.path = entry.path().string();
            dirEntry.name = entry.path().filename().string();
            // Reuse iterator-cached status when available. status() retains the
            // existing behavior of reporting a symlink target's type.
            std::error_code typeEc;
            auto status = entry.status(typeEc);
            dirEntry.type = typeEc ? EntityType::NotFound : detail::getEntityType(status);

            if (dirEntry.type == EntityType::File) {
                std::error_code sizeEc;
                dirEntry.size = static_cast<int64_t>(entry.file_size(sizeEc));
            }

            entries.push_back(std::move(dirEntry));
        };

        if (recursive) {
            for (const auto& entry : fs::recursive_directory_iterator(path, ec)) {
                processEntry(entry);
            }
        } else {
            for (const auto& entry : fs::directory_iterator(path, ec)) {
                processEntry(entry);
            }
        }

        if (ec) {
            throw std::runtime_error("Failed to list directory: " + ec.message());
        }

        return entries;
    }

    /**
     * @brief Move/rename a directory
     */
    auto moveDirectory(const std::string& sourcePath, const std::string& destinationPath) -> void {
        std::error_code ec;
        fs::rename(sourcePath, destinationPath, ec);

        if (ec) {
            throw std::runtime_error("Failed to move directory: " + ec.message());
        }
    }

    // ========================================================================
    // Path Operations (Pure, no filesystem access)
    // ========================================================================

    /**
     * @brief Get parent directory path
     */
    [[nodiscard]] static auto getParentPath(const std::string& path) -> std::string {
        return fs::path(path).parent_path().string();
    }

    /**
     * @brief Get filename from path
     */
    [[nodiscard]] static auto getFileName(const std::string& path) -> std::string {
        return fs::path(path).filename().string();
    }

    /**
     * @brief Get file extension (including dot, e.g., ".txt")
     */
    [[nodiscard]] static auto getFileExtension(const std::string& path) -> std::string {
        return fs::path(path).extension().string();
    }

    /**
     * @brief Get filename without extension
     */
    [[nodiscard]] static auto getFileNameWithoutExtension(const std::string& path) -> std::string {
        return fs::path(path).stem().string();
    }

    /**
     * @brief Join path components
     */
    [[nodiscard]] static auto joinPaths(
        const std::string& basePath,
        const std::string& relativePath
    ) -> std::string {
        return (fs::path(basePath) / relativePath).string();
    }

    /**
     * @brief Join multiple path components
     */
    [[nodiscard]] static auto joinPaths(const std::vector<std::string>& paths) -> std::string {
        if (paths.empty()) {
            return "";
        }

        fs::path result = paths[0];
        for (size_t i = 1; i < paths.size(); ++i) {
            result /= paths[i];
        }

        return result.string();
    }

    /**
     * @brief Get absolute path
     */
    [[nodiscard]] auto getAbsolutePath(const std::string& path) const -> std::string {
        std::error_code ec;
        auto absolutePath = fs::absolute(path, ec);

        if (ec) {
            throw std::runtime_error("Failed to get absolute path: " + ec.message());
        }

        return absolutePath.string();
    }

    /**
     * @brief Normalize path (resolve . and ..)
     */
    [[nodiscard]] auto normalizePath(const std::string& path) const -> std::string {
        std::error_code ec;

        // For existing paths, use canonical
        if (fs::exists(path, ec)) {
            auto canonical = fs::canonical(path, ec);
            if (!ec) {
                return canonical.string();
            }
        }

        // For non-existing paths, use lexically_normal
        return fs::path(path).lexically_normal().string();
    }

    // ========================================================================
    // Storage Information
    // ========================================================================

    /**
     * @brief Get available storage space in bytes
     */
    [[nodiscard]] auto getAvailableSpace(const std::string& path) const -> int64_t {
        std::error_code ec;
        auto spaceInfo = fs::space(path, ec);

        if (ec) {
            throw std::runtime_error("Failed to get storage info: " + ec.message());
        }

        return static_cast<int64_t>(spaceInfo.available);
    }

    /**
     * @brief Get total storage space in bytes
     */
    [[nodiscard]] auto getTotalSpace(const std::string& path) const -> int64_t {
        std::error_code ec;
        auto spaceInfo = fs::space(path, ec);

        if (ec) {
            throw std::runtime_error("Failed to get storage info: " + ec.message());
        }

        return static_cast<int64_t>(spaceInfo.capacity);
    }

    // ========================================================================
    // Hash Operations
    // ========================================================================

    /**
     * @brief Calculate hash of file content
     * @param path File path
     * @param algorithm Hash algorithm to use
     * @return Hex string of the hash
     */
    [[nodiscard]] auto calcHash(
        const std::string& path,
        HashAlgorithm algorithm = HashAlgorithm::SHA256
    ) const -> std::string {
        checkPath(path);
        auto hashFile = [&path](auto hasher) {
            std::ifstream file(path, std::ios::binary);
            if (!file) throw std::runtime_error("Cannot open file for hashing: " + path);
            std::array<char, 64 * 1024> chunk;
            while (file) {
                file.read(chunk.data(), chunk.size());
                auto count = file.gcount();
                if (count > 0) hasher.add(chunk.data(), static_cast<size_t>(count));
            }
            if (file.bad()) throw std::runtime_error("Cannot read file for hashing: " + path);
            return hasher.getHash();
        };

        switch (algorithm) {
            case HashAlgorithm::MD5: {
                return hashFile(MD5{});
            }
            case HashAlgorithm::SHA1: {
                return hashFile(SHA1{});
            }
            case HashAlgorithm::SHA256: {
                return hashFile(SHA256{});
            }
            case HashAlgorithm::SHA3_224: {
                return hashFile(SHA3(SHA3::Bits224));
            }
            case HashAlgorithm::SHA3_256: {
                return hashFile(SHA3(SHA3::Bits256));
            }
            case HashAlgorithm::SHA3_384: {
                return hashFile(SHA3(SHA3::Bits384));
            }
            case HashAlgorithm::SHA3_512: {
                return hashFile(SHA3(SHA3::Bits512));
            }
            case HashAlgorithm::Keccak224: {
                return hashFile(Keccak(Keccak::Keccak224));
            }
            case HashAlgorithm::Keccak256: {
                return hashFile(Keccak(Keccak::Keccak256));
            }
            case HashAlgorithm::Keccak384: {
                return hashFile(Keccak(Keccak::Keccak384));
            }
            case HashAlgorithm::Keccak512: {
                return hashFile(Keccak(Keccak::Keccak512));
            }
            case HashAlgorithm::CRC32: {
                return hashFile(CRC32{});
            }
            default:
                // Fallback to SHA256
                return hashFile(SHA256{});
        }
    }

};

} // namespace io

#endif // IO_FILE_SYSTEM_HPP
