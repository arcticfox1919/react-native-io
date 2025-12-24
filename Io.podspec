require "json"

package = JSON.parse(File.read(File.join(__dir__, "package.json")))

Pod::Spec.new do |s|
  s.name         = "Io"
  s.version      = package["version"]
  s.summary      = package["description"]
  s.homepage     = package["homepage"]
  s.license      = package["license"]
  s.authors      = package["author"]

  s.platforms    = { :ios => min_ios_version_supported }
  s.source       = { :git => "https://github.com/arcticfox1919/react-native-io.git", :tag => "#{s.version}" }

  # Source files:
  s.source_files = [
    "ios/**/*.{h,m,mm,swift}",
    "cpp/src/*.{h,hpp,cpp}",
    "cpp/hash/*.{h,cpp}",
    "cpp/network/*.{h,hpp}"
  ]

  # Exclude Android-specific files
  s.exclude_files = [
    "cpp/src/LoggerAndroid.cpp",
    "cpp/src/JniHelper.hpp"
  ]

  s.private_header_files = [
    "ios/**/*.h",
    "cpp/src/*.h",
    "cpp/hash/*.h",
    "cpp/network/*.h"
  ]

  # Enable high optimization for C++ code in Release builds
  s.pod_target_xcconfig = {
    "GCC_OPTIMIZATION_LEVEL" => "3",
    "OTHER_CPLUSPLUSFLAGS" => "-DNDEBUG -O3",
    "CLANG_CXX_LANGUAGE_STANDARD" => "c++20",
    "HEADER_SEARCH_PATHS" => "$(PODS_TARGET_SRCROOT)/cpp/src $(PODS_TARGET_SRCROOT)/cpp/hash $(PODS_TARGET_SRCROOT)/cpp/network"
  }

  install_modules_dependencies(s)
end
