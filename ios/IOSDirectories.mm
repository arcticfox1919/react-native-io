/**
 * @file IOSDirectories.mm
 * @brief iOS directory path implementations
 *
 * Provides access to iOS standard directories using Foundation APIs.
 *
 * Copyright (c) 2025 arcticfox
 */

#if defined(__APPLE__)

#include "IOSDirectories.hpp"
#import <Foundation/Foundation.h>
#include <mutex>

namespace rct_io {

namespace detail {
    // Cached directory paths
    inline std::string _documentsDir;
    inline std::string _libraryDir;
    inline std::string _cachesDir;
    inline std::string _tempDir;
    inline std::string _appSupportDir;
    inline std::string _bundleDir;
    inline std::string _homeDir;

    inline std::once_flag _documentsDirFlag;
    inline std::once_flag _libraryDirFlag;
    inline std::once_flag _cachesDirFlag;
    inline std::once_flag _tempDirFlag;
    inline std::once_flag _appSupportDirFlag;
    inline std::once_flag _bundleDirFlag;
    inline std::once_flag _homeDirFlag;

    inline std::string searchPathDirectory(NSSearchPathDirectory directory) {
        NSArray<NSString *> *paths = NSSearchPathForDirectoriesInDomains(
            directory, NSUserDomainMask, YES);
        if (paths.count > 0) {
            return std::string([paths.firstObject UTF8String]);
        }
        return "";
    }
}

std::string getDocumentsDirectory() {
    std::call_once(detail::_documentsDirFlag, []() {
        detail::_documentsDir = detail::searchPathDirectory(NSDocumentDirectory);
    });
    return detail::_documentsDir;
}

std::string getLibraryDirectory() {
    std::call_once(detail::_libraryDirFlag, []() {
        detail::_libraryDir = detail::searchPathDirectory(NSLibraryDirectory);
    });
    return detail::_libraryDir;
}

std::string getCachesDirectory() {
    std::call_once(detail::_cachesDirFlag, []() {
        detail::_cachesDir = detail::searchPathDirectory(NSCachesDirectory);
    });
    return detail::_cachesDir;
}

std::string getTemporaryDirectory() {
    std::call_once(detail::_tempDirFlag, []() {
        NSString *tempPath = NSTemporaryDirectory();
        if (tempPath) {
            // Remove trailing slash if present
            if ([tempPath hasSuffix:@"/"]) {
                tempPath = [tempPath substringToIndex:tempPath.length - 1];
            }
            detail::_tempDir = std::string([tempPath UTF8String]);
        }
    });
    return detail::_tempDir;
}

std::string getApplicationSupportDirectory() {
    std::call_once(detail::_appSupportDirFlag, []() {
        detail::_appSupportDir = detail::searchPathDirectory(NSApplicationSupportDirectory);
    });
    return detail::_appSupportDir;
}

std::string getBundleDirectory() {
    std::call_once(detail::_bundleDirFlag, []() {
        NSString *bundlePath = [[NSBundle mainBundle] bundlePath];
        if (bundlePath) {
            detail::_bundleDir = std::string([bundlePath UTF8String]);
        }
    });
    return detail::_bundleDir;
}

std::string getHomeDirectory() {
    std::call_once(detail::_homeDirFlag, []() {
        NSString *homePath = NSHomeDirectory();
        if (homePath) {
            detail::_homeDir = std::string([homePath UTF8String]);
        }
    });
    return detail::_homeDir;
}

} // namespace rct_io

#endif // __APPLE__
