/**
 * @file FSHostObject.hpp
 * @brief JSI Host Object binding for IOFileSystem
 *
 * Provides filesystem operations to JavaScript via JSI.
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

#ifndef IO_HOST_OBJECT_HPP
#define IO_HOST_OBJECT_HPP

#include "BS_thread_pool.hpp"
#include "ThreadPoolFactory.hpp"
#include "ThreadPoolReaper.hpp"
#include "JSIHostObjectBase.hpp"
#include "JSIArguments.hpp"
#include "IOFileSystem.hpp"
#include "IOFileHandle.hpp"
#include <ReactCommon/CallInvoker.h>
#include <mutex>
#include <atomic>

namespace rct_io {

using namespace facebook::jsi;
using namespace jsi_utils;

// Type aliases to avoid commas in macro arguments
using AsyncResultMap = std::unordered_map<std::string, AsyncResult>;


// ============================================================================
// Thread Pool Task Executor
// ============================================================================

/**
 * @brief TaskExecutor implementation using BS::thread_pool
 */
class ThreadPoolExecutor : public TaskExecutor {
private:
    size_t threadCount_;
    std::mutex lifecycleMutex_;
    std::shared_ptr<BS::thread_pool<>> pool_;
    bool closed_ = false;

public:
    explicit ThreadPoolExecutor(size_t threadCount)
        : threadCount_(threadCount) {}

    ~ThreadPoolExecutor() override {
        try {
            shutdown();
        } catch (...) {
            // Destructors must not throw. If handing the pool to the reaper
            // fails (for example, during allocation failure), the local
            // shared_ptr still performs the normal blocking pool teardown.
        }
    }

    void execute(std::function<void()>&& task) override {
        std::lock_guard lock(lifecycleMutex_);
        if (closed_) {
            throw std::runtime_error("File system context has been closed");
        }
        if (!pool_) {
            auto pool = makeThreadPool(threadCount_);
            (void)ThreadPoolReaper::instance();
            pool_ = std::move(pool);
        }
        // Keep the lifecycle lock until the task is accepted. shutdown() can
        // then make a precise guarantee: earlier tasks drain, later tasks fail.
        pool_->detach_task(std::move(task));
    }

    bool shutdown(std::function<void()> afterJoin = {}) {
        std::shared_ptr<BS::thread_pool<>> pool;
        {
            std::lock_guard lock(lifecycleMutex_);
            if (closed_) return false;
            closed_ = true;
            pool = std::move(pool_);
        }
        // A context used only for path-based synchronous operations owns no
        // worker or deferred cleanup and should not start the reaper thread.
        if (!pool && !afterJoin) return true;
        ThreadPoolReaper::instance().retire(
            std::move(pool), std::move(afterJoin));
        return true;
    }
};

// ============================================================================
// FSHostObject Implementation
// ============================================================================

/**
 * @brief Adapter to wrap React Native's CallInvoker for JSIHostObjectBase
 */
class RNCallInvokerAdapter : public JSCallInvokerWrapper {
private:
    std::shared_ptr<facebook::react::CallInvoker> invoker_;

public:
    RNCallInvokerAdapter(
        std::shared_ptr<facebook::react::CallInvoker> invoker,
        Runtime& /*runtime*/
    ) : invoker_(std::move(invoker)) {}

    void invokeAsync(std::function<void(Runtime&)>&& func) override {
        invoker_->invokeAsync(std::move(func));
    }
};

class FSHostObject : public JSIHostObjectBase<FSHostObject> {
    friend class JSIHostObjectBase<FSHostObject>;

private:
    std::shared_ptr<IOFileSystem> fs_;
    std::shared_ptr<JSCallInvokerWrapper> invoker_;
    std::unique_ptr<ThreadPoolExecutor> executor_;
    std::atomic<bool> disposed_{false};

    // File handle management (use shared_ptr to avoid dangling pointers in async ops)
    std::unordered_map<int, std::shared_ptr<IOFileHandle>> fileHandles_;
    std::mutex handlesMutex_;
    std::atomic<uint64_t> nextHandleId_{1};

    /**
     * @brief Get file handle by ID (thread-safe)
     * @returns shared_ptr to handle (safe for async operations)
     * @throws std::runtime_error if handle is invalid
     */
    std::shared_ptr<IOFileHandle> getHandle(int handleId) {
        std::lock_guard<std::mutex> lock(handlesMutex_);
        auto it = fileHandles_.find(handleId);
        if (it == fileHandles_.end()) {
            throw std::runtime_error("Invalid file handle: " + std::to_string(handleId));
        }
        return it->second;  // Return shared_ptr, increases ref count
    }

    /**
     * @brief Remove handle from map (does not close file)
     * @returns shared_ptr to removed handle, or nullptr if not found
     */
    std::shared_ptr<IOFileHandle> removeHandle(int handleId) {
        std::lock_guard<std::mutex> lock(handlesMutex_);
        auto it = fileHandles_.find(handleId);
        if (it == fileHandles_.end()) {
            return nullptr;
        }
        auto handle = std::move(it->second);
        fileHandles_.erase(it);
        return handle;
    }

    bool hasOpenHandles() {
        std::lock_guard<std::mutex> lock(handlesMutex_);
        return !fileHandles_.empty();
    }

    std::vector<std::shared_ptr<IOFileHandle>> takeOpenHandles() {
        std::vector<std::shared_ptr<IOFileHandle>> handles;
        std::lock_guard<std::mutex> lock(handlesMutex_);
        handles.reserve(fileHandles_.size());
        for (auto& [id, handle] : fileHandles_) {
            (void)id;
            handles.push_back(std::move(handle));
        }
        fileHandles_.clear();
        return handles;
    }

    static void closeHandles(
        std::vector<std::shared_ptr<IOFileHandle>> handles) noexcept {
        for (auto& handle : handles) {
            try {
                if (handle) handle->close();
            } catch (...) {
                // dispose() cannot report asynchronous cleanup errors. The
                // handle is still released and cannot be reused afterward.
            }
        }
    }

    void closeOpenHandlesAfterDrain() noexcept {
        closeHandles(takeOpenHandles());
    }

    void validateLifecycle(const std::string& name) const {
        if (name != "dispose" && disposed_.load(std::memory_order_acquire)) {
            throw std::runtime_error("File system context has been closed");
        }
    }

public:
    /**
     * @brief Construct FSHostObject with React Native's CallInvoker
     * @param runtime JSI Runtime reference (must outlive this object)
     * @param threadCount Worker count to create lazily on first async operation
     * @param callInvoker React Native's CallInvoker for JS thread callbacks
     */
    FSHostObject(
        Runtime& runtime,
        size_t threadCount,
        std::shared_ptr<facebook::react::CallInvoker> callInvoker
    ) : fs_(std::make_shared<IOFileSystem>())
      , invoker_(std::make_shared<RNCallInvokerAdapter>(std::move(callInvoker), runtime))
      , executor_(std::make_unique<ThreadPoolExecutor>(threadCount))
    {
        callInvoker_ = invoker_;
        init();  // Calls initProperties() then initMethods()
    }

    ~FSHostObject() override {
        // Normal GC teardown also avoids joining I/O workers on the JS thread.
        // No tasks can still use this object here: async tasks retain a shared
        // owner until their JS completion callback is released.
        auto handles = takeOpenHandles();
        std::shared_ptr<std::vector<std::shared_ptr<IOFileHandle>>> deferred;
        try {
            if (handles.empty()) {
                executor_->shutdown();
            } else {
                deferred = std::make_shared<
                    std::vector<std::shared_ptr<IOFileHandle>>>(
                        std::move(handles));
                if (!executor_->shutdown(
                    [deferred]() mutable noexcept {
                        closeHandles(std::move(*deferred));
                    })) {
                    closeHandles(std::move(*deferred));
                }
            }
        } catch (...) {
            if (deferred) closeHandles(std::move(*deferred));
            else closeHandles(std::move(handles));
        }
    }

    static void validateArguments(Runtime& rt, const std::string& name, const Value* args, size_t count) {
        validateFSArguments(rt, name, args, count);
    }

    // Required for async methods
    TaskExecutor* getTaskExecutor() { return executor_.get(); }

private:
    // ========================================================================
    // Property Registration (called by init())
    // ========================================================================
    void initProperties() {
        JSI_PROPERTY(version, {
            return JSI_STRING("1.0.0");
        })

        JSI_PROPERTY(platform, {
            #if defined(__APPLE__)
                return JSI_STRING("ios");
            #elif defined(__ANDROID__)
                return JSI_STRING("android");
            #else
                return JSI_STRING("unknown");
            #endif
        })
    }

    // ========================================================================
    // Method Registration (called by init())
    // ========================================================================
    void initMethods() {
        // Idempotently stop this explicit context. Tasks accepted before this
        // call finish on the pool; native handle cleanup runs after they drain.
        JSI_SYNC_METHOD(dispose, 0, {
            if (disposed_.exchange(true, std::memory_order_acq_rel)) {
                return JSI_UNDEFINED;
            }
            try {
                if (hasOpenHandles()) {
                    auto owner = this->shared_from_this();
                    executor_->shutdown([owner = std::move(owner)]() noexcept {
                        owner->closeOpenHandlesAfterDrain();
                    });
                } else {
                    executor_->shutdown();
                }
            } catch (...) {
                // A reaper startup/queue allocation failure destroys and joins
                // the detached pool while unwinding. Finish handle cleanup now.
                closeOpenHandlesAfterDrain();
            }
            return JSI_UNDEFINED;
        })

        // ====================================================================
        // File Handle Operations (Synchronous - quick operations)
        // ====================================================================

        // openFile(path, mode?) -> handle (number)
        // mode: 0='r', 1='w', 2='a', 3='r+', 4='w+', 5='a+'
        JSI_SYNC_METHOD(openFile, 2, {
            auto path = JSI_ARG_STR(0);
            auto mode = count > 1 && !args[1].isUndefined()
                ? static_cast<FileOpenMode>(static_cast<int>(JSI_ARG_NUM(1)))
                : FileOpenMode::Read;

            auto handle = std::make_shared<IOFileHandle>(path, mode);
            auto nextId = nextHandleId_++;
            if (nextId > 2147483647) throw std::overflow_error("File handle identifiers exhausted");
            int handleId = static_cast<int>(nextId);

            {
                std::lock_guard<std::mutex> lock(handlesMutex_);
                fileHandles_[handleId] = handle;
            }

            return JSI_NUM(handleId);
        })

        // fileClose(handle) -> void
        // Note: Remove from map first (under lock), then close outside lock
        JSI_SYNC_METHOD(fileClose, 1, {
            int handleId = static_cast<int>(JSI_ARG_NUM(0));
            // Remove from map (lock-protected)
            auto handle = removeHandle(handleId);
            // Close outside lock to avoid deadlock
            if (handle) {
                handle->close();
            }
            return JSI_UNDEFINED;
        })

        // fileCloseAsync(handle) -> Promise<void>
        // Prefer this when another operation may still own the handle mutex so
        // the JS thread never waits for slow storage while closing.
        JSI_ASYNC_METHOD(fileCloseAsync, 1, {
            int handleId = static_cast<int>(JSI_N_ARG(0));
            auto handle = removeHandle(handleId);
            if (handle) handle->close();
            return AsyncResult{};
        })

        // fileSeek(handle, offset, origin?) -> Promise<position>
        JSI_ASYNC_METHOD(fileSeek, 3, {
            auto handle = getHandle(static_cast<int>(JSI_N_ARG(0)));
            int64_t offset = static_cast<int64_t>(JSI_N_ARG(1));
            auto origin = JSI_N_OPT(2, 0) > 0
                ? static_cast<SeekOrigin>(static_cast<int>(JSI_N_OPT(2, 0)))
                : SeekOrigin::Begin;
            return AsyncResult(static_cast<double>(handle->seek(offset, origin)));
        })

        // fileRewind(handle) -> Promise<void>
        JSI_ASYNC_METHOD(fileRewind, 1, {
            auto handle = getHandle(static_cast<int>(JSI_N_ARG(0)));
            handle->rewind();
            return AsyncResult();
        })

        // fileGetPosition(handle) -> Promise<number>
        JSI_ASYNC_METHOD(fileGetPosition, 1, {
            auto handle = getHandle(static_cast<int>(JSI_N_ARG(0)));
            return AsyncResult(static_cast<double>(handle->getPosition()));
        })

        // fileGetSize(handle) -> Promise<number>
        JSI_ASYNC_METHOD(fileGetSize, 1, {
            auto handle = getHandle(static_cast<int>(JSI_N_ARG(0)));
            return AsyncResult(static_cast<double>(handle->getSize()));
        })

        // fileIsEOF(handle) -> Promise<boolean>
        JSI_ASYNC_METHOD(fileIsEOF, 1, {
            auto handle = getHandle(static_cast<int>(JSI_N_ARG(0)));
            return AsyncResult(handle->isEOF());
        })

        // ====================================================================
        // Synchronous Methods (original filesystem operations)
        // ====================================================================

        // Query operations (1 param: path)
        JSI_SYNC_METHOD(existsSync, 1, {
            return JSI_BOOL(fs_->exists(JSI_ARG_STR(0)));
        })

        JSI_SYNC_METHOD(isFileSync, 1, {
            return JSI_BOOL(fs_->isFile(JSI_ARG_STR(0)));
        })

        JSI_SYNC_METHOD(isDirectorySync, 1, {
            return JSI_BOOL(fs_->isDirectory(JSI_ARG_STR(0)));
        })

        JSI_SYNC_METHOD(getMetadataSync, 1, {
            return fs_->getMetadata(JSI_ARG_STR(0)).toJSObject(rt);
        })

        JSI_SYNC_METHOD(getFileSizeSync, 1, {
            return JSI_NUM(fs_->getFileSize(JSI_ARG_STR(0)));
        })

        JSI_SYNC_METHOD(getModifiedTimeSync, 1, {
            return JSI_NUM(fs_->getModifiedTime(JSI_ARG_STR(0)));
        })

        // Read operations (1 param: path)
        JSI_SYNC_METHOD(readStringSync, 1, {
            return JSI_STRING(fs_->readString(JSI_ARG_STR(0)));
        })

        JSI_SYNC_METHOD(readBytesSync, 1, {
            auto bytes = fs_->readBytes(JSI_ARG_STR(0));
            return toArrayBuffer(rt, std::move(bytes));
        })

        // Write operations (4 params: path, content, mode?, createParents?)
        JSI_SYNC_METHOD(writeStringSync, 4, {
            fs_->writeString(
                JSI_ARG_STR(0),
                JSI_ARG_STR(1),
                static_cast<WriteMode>(static_cast<int>(JSI_ARG_NUM_OPT(2, 0))),
                JSI_ARG_BOOL_OPT(3, false)
            );
            return JSI_UNDEFINED;
        })

        JSI_SYNC_METHOD(writeBytesSync, 4, {
            auto bytes = borrowBytes(rt, args[1]);
            // Keep the JSI owner alive and borrow only for this synchronous call.
            // Never capture this view in an asynchronous task.
            fs_->writeBytes(
                JSI_ARG_STR(0),
                bytes.view(rt),
                static_cast<WriteMode>(static_cast<int>(JSI_ARG_NUM_OPT(2, 0))),
                JSI_ARG_BOOL_OPT(3, false)
            );
            return JSI_UNDEFINED;
        })

        // File management
        JSI_SYNC_METHOD(createFileSync, 2, {  // path, createParents?
            fs_->createFile(JSI_ARG_STR(0), JSI_ARG_BOOL_OPT(1, false));
            return JSI_UNDEFINED;
        })

        JSI_SYNC_METHOD(deleteFileSync, 1, {  // path
            return JSI_BOOL(fs_->deleteFile(JSI_ARG_STR(0)));
        })

        JSI_SYNC_METHOD(copyFileSync, 3, {  // src, dest, overwrite?
            fs_->copyFile(JSI_ARG_STR(0), JSI_ARG_STR(1), JSI_ARG_BOOL_OPT(2, true));
            return JSI_UNDEFINED;
        })

        JSI_SYNC_METHOD(moveFileSync, 2, {  // src, dest
            fs_->moveFile(JSI_ARG_STR(0), JSI_ARG_STR(1));
            return JSI_UNDEFINED;
        })

        // Directory operations
        JSI_SYNC_METHOD(createDirectorySync, 2, {  // path, recursive?
            fs_->createDirectory(JSI_ARG_STR(0), JSI_ARG_BOOL_OPT(1, false));
            return JSI_UNDEFINED;
        })

        JSI_SYNC_METHOD(deleteDirectorySync, 2, {  // path, recursive?
            return JSI_NUM(fs_->deleteDirectory(JSI_ARG_STR(0), JSI_ARG_BOOL_OPT(1, false)));
        })

        JSI_SYNC_METHOD(listDirectorySync, 2, {  // path, recursive?
            auto entries = fs_->listDirectory(JSI_ARG_STR(0), JSI_ARG_BOOL_OPT(1, false));
            Array arr(rt, entries.size());
            for (size_t i = 0; i < entries.size(); ++i) {
                arr.setValueAtIndex(rt, i, entries[i].toJSObject(rt));
            }
            return std::move(arr);
        })

        JSI_SYNC_METHOD(moveDirectorySync, 2, {  // src, dest
            fs_->moveDirectory(JSI_ARG_STR(0), JSI_ARG_STR(1));
            return JSI_UNDEFINED;
        })

        // Path operations (pure, no I/O)
        JSI_SYNC_METHOD(getParentPath, 1, {
            return JSI_STRING(IOFileSystem::getParentPath(JSI_ARG_STR(0)));
        })

        JSI_SYNC_METHOD(getFileName, 1, {
            return JSI_STRING(IOFileSystem::getFileName(JSI_ARG_STR(0)));
        })

        JSI_SYNC_METHOD(getFileExtension, 1, {
            return JSI_STRING(IOFileSystem::getFileExtension(JSI_ARG_STR(0)));
        })

        JSI_SYNC_METHOD(getFileNameWithoutExtension, 1, {
            return JSI_STRING(IOFileSystem::getFileNameWithoutExtension(JSI_ARG_STR(0)));
        })

        JSI_SYNC_METHOD(joinPaths, 0, {  // variable args
            std::vector<std::string> paths(count);
            for (size_t i = 0; i < count; ++i) {
                paths[i] = JSI_ARG_STR(i);
            }
            return JSI_STRING(IOFileSystem::joinPaths(paths));
        })

        JSI_SYNC_METHOD(getAbsolutePathSync, 1, {
            return JSI_STRING(fs_->getAbsolutePath(JSI_ARG_STR(0)));
        })

        JSI_SYNC_METHOD(normalizePathSync, 1, {
            return JSI_STRING(fs_->normalizePath(JSI_ARG_STR(0)));
        })

        // Storage info (1 param: path)
        JSI_SYNC_METHOD(getAvailableSpaceSync, 1, {
            return JSI_NUM(fs_->getAvailableSpace(JSI_ARG_STR(0)));
        })

        JSI_SYNC_METHOD(getTotalSpaceSync, 1, {
            return JSI_NUM(fs_->getTotalSpace(JSI_ARG_STR(0)));
        })

        // ====================================================================
        // Async Methods
        // ====================================================================
        // Note: Async methods return AsyncResult (pure C++ data),
        // which is converted to JS Value on the JS thread for thread safety.

        // Query operations (1 param: path)
        JSI_ASYNC_METHOD(exists, 1, {
            return AsyncResult(fs_->exists(JSI_S_ARG(0)));
        })

        JSI_ASYNC_METHOD(isFile, 1, {
            return AsyncResult(fs_->isFile(JSI_S_ARG(0)));
        })

        JSI_ASYNC_METHOD(isDirectory, 1, {
            return AsyncResult(fs_->isDirectory(JSI_S_ARG(0)));
        })

        JSI_ASYNC_METHOD(getMetadata, 1, {
            auto meta = fs_->getMetadata(JSI_S_ARG(0));
            AsyncResultMap obj(3);
            obj.emplace("size", static_cast<double>(meta.size));
            obj.emplace("modifiedTime", static_cast<double>(meta.modifiedTime));
            obj.emplace("type", static_cast<double>(static_cast<int>(meta.type)));
            return AsyncResult(std::move(obj));
        })

        JSI_ASYNC_METHOD(getFileSize, 1, {
            return AsyncResult(static_cast<double>(fs_->getFileSize(JSI_S_ARG(0))));
        })

        JSI_ASYNC_METHOD(getModifiedTime, 1, {
            return AsyncResult(static_cast<double>(fs_->getModifiedTime(JSI_S_ARG(0))));
        })

        // Read operations (1 param: path)
        JSI_ASYNC_METHOD(readString, 1, {
            return AsyncResult(fs_->readString(JSI_S_ARG(0)));
        })

        JSI_ASYNC_METHOD(readBytes, 1, {
            return AsyncResult(fs_->readBytes(JSI_S_ARG(0)));
        })

        // Write operations (4 params: path, content, mode?, createParents?)
        JSI_ASYNC_METHOD(writeString, 4, {
            fs_->writeString(JSI_S_ARG(0), JSI_S_ARG(1),
                static_cast<WriteMode>(static_cast<int>(JSI_N_OPT(0, 0))),
                JSI_B_OPT(0, false));
            return AsyncResult();  // void result
        })

        JSI_ASYNC_METHOD(writeBytes, 4, {
            fs_->writeBytes(JSI_S_ARG(0), JSI_BUF_ARG(0),
                static_cast<WriteMode>(static_cast<int>(JSI_N_OPT(0, 0))),
                JSI_B_OPT(0, false));
            return AsyncResult();  // void result
        })

        // File management
        JSI_ASYNC_METHOD(createFile, 2, {  // path, createParents?
            fs_->createFile(JSI_S_ARG(0), JSI_B_OPT(0, false));
            return AsyncResult();
        })

        JSI_ASYNC_METHOD(deleteFile, 1, {  // path
            return AsyncResult(fs_->deleteFile(JSI_S_ARG(0)));
        })

        JSI_ASYNC_METHOD(copyFile, 3, {  // src, dest, overwrite?
            fs_->copyFile(JSI_S_ARG(0), JSI_S_ARG(1), JSI_B_OPT(0, true));
            return AsyncResult();
        })

        JSI_ASYNC_METHOD(moveFile, 2, {  // src, dest
            fs_->moveFile(JSI_S_ARG(0), JSI_S_ARG(1));
            return AsyncResult();
        })

        // Directory operations
        JSI_ASYNC_METHOD(createDirectory, 2, {  // path, recursive?
            fs_->createDirectory(JSI_S_ARG(0), JSI_B_OPT(0, false));
            return AsyncResult();
        })

        JSI_ASYNC_METHOD(deleteDirectory, 2, {  // path, recursive?
            return AsyncResult(static_cast<double>(fs_->deleteDirectory(JSI_S_ARG(0), JSI_B_OPT(0, false))));
        })

        JSI_ASYNC_METHOD(listDirectory, 2, {  // path, recursive?
            auto entries = fs_->listDirectory(JSI_S_ARG(0), JSI_B_OPT(0, false));
            std::vector<AsyncResult> arr;
            arr.reserve(entries.size());
            for (const auto& entry : entries) {
                AsyncResultMap obj(4);
                obj.emplace("path", entry.path);
                obj.emplace("name", entry.name);
                obj.emplace("type", static_cast<double>(static_cast<int>(entry.type)));
                obj.emplace("size", static_cast<double>(entry.size));
                arr.emplace_back(std::move(obj));
            }
            return AsyncResult(std::move(arr));
        })

        JSI_ASYNC_METHOD(moveDirectory, 2, {  // src, dest
            fs_->moveDirectory(JSI_S_ARG(0), JSI_S_ARG(1));
            return AsyncResult();
        })

        // Path operations (1 param: path)
        JSI_ASYNC_METHOD(getAbsolutePath, 1, {
            return AsyncResult(fs_->getAbsolutePath(JSI_S_ARG(0)));
        })

        JSI_ASYNC_METHOD(normalizePath, 1, {
            return AsyncResult(fs_->normalizePath(JSI_S_ARG(0)));
        })

        // Storage info (1 param: path)
        JSI_ASYNC_METHOD(getAvailableSpace, 1, {
            return AsyncResult(static_cast<double>(fs_->getAvailableSpace(JSI_S_ARG(0))));
        })

        JSI_ASYNC_METHOD(getTotalSpace, 1, {
            return AsyncResult(static_cast<double>(fs_->getTotalSpace(JSI_S_ARG(0))));
        })

        // Hash (2 params: path, algorithm?)
        JSI_ASYNC_METHOD(calcHash, 2, {
            auto algorithm = static_cast<HashAlgorithm>(static_cast<int>(JSI_N_OPT(0, 2)));
            return AsyncResult(fs_->calcHash(JSI_S_ARG(0), algorithm));
        })

        // ====================================================================
        // File Handle Async Operations (I/O bound - use thread pool)
        // ====================================================================

        // fileFlush(handle) -> void (I/O operation - async)
        JSI_ASYNC_METHOD(fileFlush, 1, {
            int handleId = static_cast<int>(JSI_N_ARG(0));
            getHandle(handleId)->flush();
            return AsyncResult();
        })

        // fileTruncate(handle) -> void (I/O operation - async)
        JSI_ASYNC_METHOD(fileTruncate, 1, {
            int handleId = static_cast<int>(JSI_N_ARG(0));
            getHandle(handleId)->truncate();
            return AsyncResult();
        })

        // fileRead(handle, size?) -> ArrayBuffer
        JSI_ASYNC_METHOD(fileRead, 2, {
            int handleId = static_cast<int>(JSI_N_ARG(0));
            int64_t size = jsiNumArgs.size() > 1 ? static_cast<int64_t>(JSI_N_ARG(1)) : -1;
            return AsyncResult(getHandle(handleId)->read(size));
        })

        // fileReadString(handle, size?) -> string
        JSI_ASYNC_METHOD(fileReadString, 2, {
            int handleId = static_cast<int>(JSI_N_ARG(0));
            int64_t size = jsiNumArgs.size() > 1 ? static_cast<int64_t>(JSI_N_ARG(1)) : -1;
            return AsyncResult(getHandle(handleId)->readString(size));
        })

        // fileReadLine(handle) -> string
        JSI_ASYNC_METHOD(fileReadLine, 1, {
            int handleId = static_cast<int>(JSI_N_ARG(0));
            return AsyncResult(getHandle(handleId)->readLine());
        })

        // fileWrite(handle, buffer) -> number (bytes written)
        JSI_ASYNC_METHOD(fileWrite, 2, {
            int handleId = static_cast<int>(JSI_N_ARG(0));
            auto& buffer = JSI_BUF_ARG(0);
            return AsyncResult(static_cast<double>(getHandle(handleId)->write(buffer)));
        })

        // fileWriteString(handle, string) -> number (bytes written)
        JSI_ASYNC_METHOD(fileWriteString, 2, {
            int handleId = static_cast<int>(JSI_N_ARG(0));
            return AsyncResult(static_cast<double>(getHandle(handleId)->writeString(JSI_S_ARG(0))));
        })

        // fileWriteLine(handle, string) -> number (bytes written)
        JSI_ASYNC_METHOD(fileWriteLine, 2, {
            int handleId = static_cast<int>(JSI_N_ARG(0));
            return AsyncResult(static_cast<double>(getHandle(handleId)->writeLine(JSI_S_ARG(0))));
        })
    }
};

} // namespace rct_io

#endif // IO_HOST_OBJECT_HPP
