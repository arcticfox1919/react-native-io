/**
 * @file JniHelper.hpp
 * @brief JNI helper utilities for Android
 *
 * Provides convenient access to Android Context directories from C++/JSI code.
 * Uses fbjni reflection framework for cleaner code.
 */

#pragma once

#include <fbjni/fbjni.h>
#include <fbjni/Context.h>
#include <fbjni/File.h>
#include <mutex>
#include <string>
#include <vector>

namespace rct_io {

// ============================================================================
// Internal Helpers
// ============================================================================

namespace detail {
    inline int _sdkVersion = 0;
    inline std::once_flag _sdkVersionFlag;

    // Cached directory paths
    inline std::string _filesDir;
    inline std::string _cacheDir;
    inline std::string _codeCacheDir;
    inline std::string _noBackupFilesDir;
    inline std::string _dataDir;
    inline std::string _externalCacheDir;
    inline std::string _obbDir;
    inline std::string _externalFilesDir;
    inline std::string _externalFilesDirDownloads;
    inline std::string _externalFilesDirPictures;
    inline std::string _externalFilesDirMovies;
    inline std::string _externalFilesDirMusic;
    inline std::string _externalFilesDirDocuments;
    inline std::string _externalFilesDirDCIM;

    inline std::once_flag _filesDirFlag;
    inline std::once_flag _cacheDirFlag;
    inline std::once_flag _codeCacheDirFlag;
    inline std::once_flag _noBackupFilesDirFlag;
    inline std::once_flag _dataDirFlag;
    inline std::once_flag _externalCacheDirFlag;
    inline std::once_flag _obbDirFlag;
    inline std::once_flag _externalFilesDirFlag;
    inline std::once_flag _externalFilesDirDownloadsFlag;
    inline std::once_flag _externalFilesDirPicturesFlag;
    inline std::once_flag _externalFilesDirMoviesFlag;
    inline std::once_flag _externalFilesDirMusicFlag;
    inline std::once_flag _externalFilesDirDocumentsFlag;
    inline std::once_flag _externalFilesDirDCIMFlag;

    inline std::vector<std::string> _externalCacheDirs;
    inline std::vector<std::string> _externalFilesDirs;
    inline std::vector<std::string> _obbDirs;
    inline std::once_flag _externalCacheDirsFlag;
    inline std::once_flag _externalFilesDirsFlag;
    inline std::once_flag _obbDirsFlag;

    // Helper: get path from JFile
    inline std::string fileToPath(facebook::jni::local_ref<facebook::jni::JFile> file) {
        return file ? file->getAbsolutePath() : "";
    }
}

// ============================================================================
// Core Functions
// ============================================================================


jobject getApplication(JNIEnv* env) {
  auto activityThreadClass = env->FindClass("android/app/ActivityThread");
  auto currentApplicationMethodID = env->GetStaticMethodID(
      activityThreadClass, "currentApplication", "()Landroid/app/Application;");
  return env->CallStaticObjectMethod(
      activityThreadClass, currentApplicationMethodID);
}

/**
 * Get the Android Application Context.
 */
facebook::jni::alias_ref<facebook::jni::AContext> getContext() {
  auto env = facebook::jni::Environment::ensureCurrentThreadIsAttached();
  auto application = getApplication(env);
  return facebook::jni::wrap_alias(
      static_cast<facebook::jni::AContext::javaobject>(application));
}

/**
 * Get the Android SDK version (Build.VERSION.SDK_INT).
 */
inline int getSdkVersion() {
    std::call_once(detail::_sdkVersionFlag, []() {
        auto env = facebook::jni::Environment::current();
        auto versionClass = env->FindClass("android/os/Build$VERSION");
        auto sdkIntField = env->GetStaticFieldID(versionClass, "SDK_INT", "I");
        detail::_sdkVersion = env->GetStaticIntField(versionClass, sdkIntField);
    });
    return detail::_sdkVersion;
}

// ============================================================================
// Internal Storage Directories
// ============================================================================

inline const std::string& getFilesDir() {
    std::call_once(detail::_filesDirFlag, []() {
        detail::_filesDir = getContext()->getFilesDir()->getAbsolutePath();
    });
    return detail::_filesDir;
}

inline const std::string& getCacheDir() {
    std::call_once(detail::_cacheDirFlag, []() {
        detail::_cacheDir = getContext()->getCacheDir()->getAbsolutePath();
    });
    return detail::_cacheDir;
}

inline const std::string& getCodeCacheDir() {
    std::call_once(detail::_codeCacheDirFlag, []() {
        auto ctx = getContext();
        static const auto method = facebook::jni::AContext::javaClassStatic()
            ->getMethod<facebook::jni::JFile()>("getCodeCacheDir");
        detail::_codeCacheDir = detail::fileToPath(method(ctx));
    });
    return detail::_codeCacheDir;
}

inline const std::string& getNoBackupFilesDir() {
    std::call_once(detail::_noBackupFilesDirFlag, []() {
        auto ctx = getContext();
        static const auto method = facebook::jni::AContext::javaClassStatic()
            ->getMethod<facebook::jni::JFile()>("getNoBackupFilesDir");
        detail::_noBackupFilesDir = detail::fileToPath(method(ctx));
    });
    return detail::_noBackupFilesDir;
}

inline const std::string& getDataDir() {
    std::call_once(detail::_dataDirFlag, []() {
        if (getSdkVersion() >= 24) {
            auto ctx = getContext();
            static const auto method = facebook::jni::AContext::javaClassStatic()
                ->getMethod<facebook::jni::JFile()>("getDataDir");
            detail::_dataDir = detail::fileToPath(method(ctx));
        }
    });
    return detail::_dataDir;
}

// ============================================================================
// External Storage Directories
// ============================================================================

inline const std::string& getExternalCacheDir() {
    std::call_once(detail::_externalCacheDirFlag, []() {
        auto ctx = getContext();
        static const auto method = facebook::jni::AContext::javaClassStatic()
            ->getMethod<facebook::jni::JFile()>("getExternalCacheDir");
        detail::_externalCacheDir = detail::fileToPath(method(ctx));
    });
    return detail::_externalCacheDir;
}

inline const std::string& getObbDir() {
    std::call_once(detail::_obbDirFlag, []() {
        auto ctx = getContext();
        static const auto method = facebook::jni::AContext::javaClassStatic()
            ->getMethod<facebook::jni::JFile()>("getObbDir");
        detail::_obbDir = detail::fileToPath(method(ctx));
    });
    return detail::_obbDir;
}

inline const std::string& getExternalFilesDir() {
    std::call_once(detail::_externalFilesDirFlag, []() {
        auto ctx = getContext();
        static const auto method = facebook::jni::AContext::javaClassStatic()
            ->getMethod<facebook::jni::JFile(facebook::jni::JString::javaobject)>("getExternalFilesDir");
        detail::_externalFilesDir = detail::fileToPath(method(ctx, nullptr));
    });
    return detail::_externalFilesDir;
}

inline const std::string& getExternalFilesDirDownloads() {
    std::call_once(detail::_externalFilesDirDownloadsFlag, []() {
        auto ctx = getContext();
        static const auto method = facebook::jni::AContext::javaClassStatic()
            ->getMethod<facebook::jni::JFile(facebook::jni::JString::javaobject)>("getExternalFilesDir");
        detail::_externalFilesDirDownloads = detail::fileToPath(
            method(ctx, facebook::jni::make_jstring("Download").get()));
    });
    return detail::_externalFilesDirDownloads;
}

inline const std::string& getExternalFilesDirPictures() {
    std::call_once(detail::_externalFilesDirPicturesFlag, []() {
        auto ctx = getContext();
        static const auto method = facebook::jni::AContext::javaClassStatic()
            ->getMethod<facebook::jni::JFile(facebook::jni::JString::javaobject)>("getExternalFilesDir");
        detail::_externalFilesDirPictures = detail::fileToPath(
            method(ctx, facebook::jni::make_jstring("Pictures").get()));
    });
    return detail::_externalFilesDirPictures;
}

inline const std::string& getExternalFilesDirMovies() {
    std::call_once(detail::_externalFilesDirMoviesFlag, []() {
        auto ctx = getContext();
        static const auto method = facebook::jni::AContext::javaClassStatic()
            ->getMethod<facebook::jni::JFile(facebook::jni::JString::javaobject)>("getExternalFilesDir");
        detail::_externalFilesDirMovies = detail::fileToPath(
            method(ctx, facebook::jni::make_jstring("Movies").get()));
    });
    return detail::_externalFilesDirMovies;
}

inline const std::string& getExternalFilesDirMusic() {
    std::call_once(detail::_externalFilesDirMusicFlag, []() {
        auto ctx = getContext();
        static const auto method = facebook::jni::AContext::javaClassStatic()
            ->getMethod<facebook::jni::JFile(facebook::jni::JString::javaobject)>("getExternalFilesDir");
        detail::_externalFilesDirMusic = detail::fileToPath(
            method(ctx, facebook::jni::make_jstring("Music").get()));
    });
    return detail::_externalFilesDirMusic;
}

inline const std::string& getExternalFilesDirDocuments() {
    std::call_once(detail::_externalFilesDirDocumentsFlag, []() {
        auto ctx = getContext();
        static const auto method = facebook::jni::AContext::javaClassStatic()
            ->getMethod<facebook::jni::JFile(facebook::jni::JString::javaobject)>("getExternalFilesDir");
        detail::_externalFilesDirDocuments = detail::fileToPath(
            method(ctx, facebook::jni::make_jstring("Documents").get()));
    });
    return detail::_externalFilesDirDocuments;
}

inline const std::string& getExternalFilesDirDCIM() {
    std::call_once(detail::_externalFilesDirDCIMFlag, []() {
        auto ctx = getContext();
        static const auto method = facebook::jni::AContext::javaClassStatic()
            ->getMethod<facebook::jni::JFile(facebook::jni::JString::javaobject)>("getExternalFilesDir");
        detail::_externalFilesDirDCIM = detail::fileToPath(
            method(ctx, facebook::jni::make_jstring("DCIM").get()));
    });
    return detail::_externalFilesDirDCIM;
}

// ============================================================================
// Multiple External Storage
// ============================================================================

inline const std::vector<std::string>& getExternalCacheDirs() {
    std::call_once(detail::_externalCacheDirsFlag, []() {
        auto ctx = getContext();
        static const auto method = facebook::jni::AContext::javaClassStatic()
            ->getMethod<facebook::jni::JArrayClass<facebook::jni::JFile>()>("getExternalCacheDirs");
        auto files = method(ctx);
        if (files) {
            for (size_t i = 0; i < files->size(); i++) {
                auto path = detail::fileToPath(files->getElement(i));
                if (!path.empty()) {
                    detail::_externalCacheDirs.push_back(path);
                }
            }
        }
    });
    return detail::_externalCacheDirs;
}

inline const std::vector<std::string>& getExternalFilesDirs() {
    std::call_once(detail::_externalFilesDirsFlag, []() {
        auto ctx = getContext();
        static const auto method = facebook::jni::AContext::javaClassStatic()
            ->getMethod<facebook::jni::JArrayClass<facebook::jni::JFile>(facebook::jni::JString::javaobject)>("getExternalFilesDirs");
        auto files = method(ctx, nullptr);
        if (files) {
            for (size_t i = 0; i < files->size(); i++) {
                auto path = detail::fileToPath(files->getElement(i));
                if (!path.empty()) {
                    detail::_externalFilesDirs.push_back(path);
                }
            }
        }
    });
    return detail::_externalFilesDirs;
}

inline const std::vector<std::string>& getObbDirs() {
    std::call_once(detail::_obbDirsFlag, []() {
        auto ctx = getContext();
        static const auto method = facebook::jni::AContext::javaClassStatic()
            ->getMethod<facebook::jni::JArrayClass<facebook::jni::JFile>()>("getObbDirs");
        auto files = method(ctx);
        if (files) {
            for (size_t i = 0; i < files->size(); i++) {
                auto path = detail::fileToPath(files->getElement(i));
                if (!path.empty()) {
                    detail::_obbDirs.push_back(path);
                }
            }
        }
    });
    return detail::_obbDirs;
}

} // namespace rct_io
