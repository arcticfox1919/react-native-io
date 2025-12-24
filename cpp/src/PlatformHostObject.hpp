/**
 * @file PlatformHostObject.hpp
 * @brief JSI Host Object for platform-specific directory paths
 *
 * Provides platform-specific directory paths to JavaScript via JSI.
 * Uses synchronous property registration only.
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

#ifndef PLATFORM_HOST_OBJECT_HPP
#define PLATFORM_HOST_OBJECT_HPP

#include "JSIHostObjectBase.hpp"

#if defined(__ANDROID__)
#include "JniHelper.hpp"
#elif defined(__APPLE__)
#include "IOSDirectories.hpp"
#endif

namespace rct_io {

using namespace facebook::jsi;
using namespace jsi_utils;

// ============================================================================
// PlatformHostObject Implementation
// ============================================================================

/**
 * @brief Host object providing platform-specific directory paths
 *
 * This is a lightweight host object that only exposes properties (no async methods).
 * All properties are synchronously computed and cached by the platform layer.
 */
class PlatformHostObject : public JSIHostObjectBase<PlatformHostObject> {
    friend class JSIHostObjectBase<PlatformHostObject>;

public:
    /**
     * @brief Construct PlatformHostObject
     * @param runtime JSI Runtime reference
     */
    explicit PlatformHostObject(Runtime& runtime) {
        (void)runtime;  // Not needed for sync-only host object
        init();  // Calls initProperties() then initMethods()
    }

private:
    // ========================================================================
    // Property Registration (called by init())
    // ========================================================================
    void initProperties() {
        // Platform identifier
        JSI_PROPERTY(platform, {
            #if defined(__APPLE__)
                return JSI_STRING("ios");
            #elif defined(__ANDROID__)
                return JSI_STRING("android");
            #else
                return JSI_STRING("unknown");
            #endif
        })

#if defined(__ANDROID__)
        // ====================================================================
        // Android Internal Storage
        // ====================================================================

        /** Internal files directory (e.g., /data/data/<pkg>/files) */
        JSI_PROPERTY(filesDir, {
            return JSI_STRING(getFilesDir());
        })

        /** Internal cache directory (e.g., /data/data/<pkg>/cache) */
        JSI_PROPERTY(cacheDir, {
            return JSI_STRING(getCacheDir());
        })

        /** Code cache directory (e.g., /data/data/<pkg>/code_cache) */
        JSI_PROPERTY(codeCacheDir, {
            return JSI_STRING(getCodeCacheDir());
        })

        /** No-backup files directory (e.g., /data/data/<pkg>/no_backup) */
        JSI_PROPERTY(noBackupFilesDir, {
            return JSI_STRING(getNoBackupFilesDir());
        })

        /** Data directory (e.g., /data/data/<pkg>), API 24+ only */
        JSI_PROPERTY(dataDir, {
            return JSI_STRING(getDataDir());
        })

        // ====================================================================
        // Android External Storage
        // ====================================================================

        /** External files directory root */
        JSI_PROPERTY(externalFilesDir, {
            return JSI_STRING(getExternalFilesDir());
        })

        /** External cache directory */
        JSI_PROPERTY(externalCacheDir, {
            return JSI_STRING(getExternalCacheDir());
        })

        /** OBB directory for expansion files */
        JSI_PROPERTY(obbDir, {
            return JSI_STRING(getObbDir());
        })

        // ====================================================================
        // Android External Storage - Media Directories
        // ====================================================================

        /** External Downloads directory */
        JSI_PROPERTY(downloadsDir, {
            return JSI_STRING(getExternalFilesDirDownloads());
        })

        /** External Pictures directory */
        JSI_PROPERTY(picturesDir, {
            return JSI_STRING(getExternalFilesDirPictures());
        })

        /** External Movies directory */
        JSI_PROPERTY(moviesDir, {
            return JSI_STRING(getExternalFilesDirMovies());
        })

        /** External Music directory */
        JSI_PROPERTY(musicDir, {
            return JSI_STRING(getExternalFilesDirMusic());
        })

        /** External Documents directory */
        JSI_PROPERTY(documentsDir, {
            return JSI_STRING(getExternalFilesDirDocuments());
        })

        /** External DCIM directory */
        JSI_PROPERTY(dcimDir, {
            return JSI_STRING(getExternalFilesDirDCIM());
        })

        /** Android SDK version */
        JSI_PROPERTY(sdkVersion, {
            return JSI_NUM(getSdkVersion());
        })

#elif defined(__APPLE__)
        // ====================================================================
        // iOS Directories
        // ====================================================================

        /** Documents directory (backed up by iCloud) */
        JSI_PROPERTY(documentsDir, {
            return JSI_STRING(getDocumentsDirectory());
        })

        /** Library directory */
        JSI_PROPERTY(libraryDir, {
            return JSI_STRING(getLibraryDirectory());
        })

        /** Caches directory (not backed up) */
        JSI_PROPERTY(cacheDir, {
            return JSI_STRING(getCachesDirectory());
        })

        /** Temporary directory (may be purged by system) */
        JSI_PROPERTY(tempDir, {
            return JSI_STRING(getTemporaryDirectory());
        })

        /** Application Support directory */
        JSI_PROPERTY(applicationSupportDir, {
            return JSI_STRING(getApplicationSupportDirectory());
        })

        /** App bundle directory (read-only) */
        JSI_PROPERTY(bundleDir, {
            return JSI_STRING(getBundleDirectory());
        })

        /** Home directory */
        JSI_PROPERTY(homeDir, {
            return JSI_STRING(getHomeDirectory());
        })

        // For compatibility with Android API
        JSI_PROPERTY(filesDir, {
            return JSI_STRING(getDocumentsDirectory());
        })

#endif
    }

    // ========================================================================
    // Method Registration (called by init())
    // ========================================================================
    void initMethods() {
        // No methods for this host object - properties only

#if defined(__ANDROID__)
        // ====================================================================
        // Android Multiple Storage (for devices with multiple storage volumes)
        // ====================================================================

        /** Get all external cache directories */
        JSI_SYNC_METHOD(getExternalCacheDirs, 0, {
            const auto& dirs = rct_io::getExternalCacheDirs();
            Array arr(rt, dirs.size());
            for (size_t i = 0; i < dirs.size(); ++i) {
                arr.setValueAtIndex(rt, i, JSI_STRING(dirs[i]));
            }
            return std::move(arr);
        })

        /** Get all external files directories */
        JSI_SYNC_METHOD(getExternalFilesDirs, 0, {
            const auto& dirs = rct_io::getExternalFilesDirs();
            Array arr(rt, dirs.size());
            for (size_t i = 0; i < dirs.size(); ++i) {
                arr.setValueAtIndex(rt, i, JSI_STRING(dirs[i]));
            }
            return std::move(arr);
        })

        /** Get all OBB directories */
        JSI_SYNC_METHOD(getObbDirs, 0, {
            const auto& dirs = rct_io::getObbDirs();
            Array arr(rt, dirs.size());
            for (size_t i = 0; i < dirs.size(); ++i) {
                arr.setValueAtIndex(rt, i, JSI_STRING(dirs[i]));
            }
            return std::move(arr);
        })
#endif
    }
};

} // namespace rct_io

#endif // PLATFORM_HOST_OBJECT_HPP
