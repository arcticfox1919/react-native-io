#pragma once

#include "IOLimits.hpp"
#include <atomic>
#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <string>
#include <system_error>
#include <sys/stat.h>
#include <unistd.h>

namespace rct_io::detail {
class UniqueFD {
    int value_;
public:
    explicit UniqueFD(int value) : value_(value) {}
    ~UniqueFD() { if (value_ >= 0) ::close(value_); }
    UniqueFD(const UniqueFD&) = delete;
    UniqueFD& operator=(const UniqueFD&) = delete;
    int get() const { return value_; }
    void closeChecked() {
        int fd = value_;
        value_ = -1; // Never retry close: on supported platforms the fd may already be reused.
        if (::close(fd) != 0) throw std::system_error(errno, std::generic_category(), "Atomic file close failed");
    }
};
inline void syncFD(int fd, const char* message) {
    int status;
    do { status = ::fsync(fd); } while (status < 0 && errno == EINTR);
    if (status < 0) throw std::system_error(errno, std::generic_category(), message);
}

// Use directory-relative syscalls so a renamed parent does not redirect commit
// or cleanup to a different directory. O_EXCL means predictable staging names
// never allow following or overwriting another file.
inline void atomicWrite(const std::string& path, const uint8_t* data, size_t size, bool durable) {
    checkPath(path);
    checkBufferSize(size);
    auto target = std::filesystem::path(path);
    auto name = target.filename().string();
    if (name.empty() || name == "." || name == "..") throw std::invalid_argument("Invalid atomic write destination");
    auto parent = target.parent_path();
    if (parent.empty()) parent = ".";
    UniqueFD directory(::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (directory.get() < 0) throw std::system_error(errno, std::generic_category(), "Cannot open atomic write directory");
    struct stat existing{};
    bool exists = ::fstatat(directory.get(), name.c_str(), &existing, AT_SYMLINK_NOFOLLOW) == 0;
    if (!exists && errno != ENOENT) throw std::system_error(errno, std::generic_category(), "Cannot inspect atomic write destination");
    if (exists && !S_ISREG(existing.st_mode)) throw std::invalid_argument("Atomic write destination must be a regular file, not a symlink or directory");
    static std::atomic<uint64_t> next{0};
    std::string stage;
    int fd = -1;
    for (int attempt = 0; attempt < 128; ++attempt) {
        stage = ".rct-io-" + std::to_string(::getpid()) + "-" + std::to_string(++next);
        fd = ::openat(directory.get(), stage.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd >= 0 || errno != EEXIST) break;
    }
    if (fd < 0) throw std::system_error(errno, std::generic_category(), "Cannot create atomic staging file");
    UniqueFD output(fd);
    struct Cleanup {
        int directory;
        const std::string& name;
        bool committed = false;
        ~Cleanup() { if (!committed) ::unlinkat(directory, name.c_str(), 0); }
    } cleanup{directory.get(), stage};
    if (exists && ::fchmod(output.get(), existing.st_mode & 0777) != 0)
        throw std::system_error(errno, std::generic_category(), "Cannot preserve file permissions");
    size_t position = 0;
    while (position < size) {
        auto written = ::write(output.get(), data + position, std::min<size_t>(size - position, 1024 * 1024));
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) throw std::system_error(written == 0 ? EIO : errno, std::generic_category(), "Atomic file write failed");
        position += static_cast<size_t>(written);
    }
    if (durable) syncFD(output.get(), "Atomic file sync failed");
    output.closeChecked();
    if (::renameat(directory.get(), stage.c_str(), directory.get(), name.c_str()) != 0)
        throw std::system_error(errno, std::generic_category(), "Atomic replacement failed");
    cleanup.committed = true;
    // If this sync fails the replacement has ALREADY committed. The error must
    // not imply rollback: the caller should read back before retrying.
    if (durable) syncFD(directory.get(), "Replacement committed but directory sync failed");
}
} // namespace rct_io::detail
