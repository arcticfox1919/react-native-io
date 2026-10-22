#pragma once

#include <AbilityKit/ability_runtime/application_context.h>
#include <array>
#include <limits.h>
#include <stdexcept>
#include <string>

namespace rct_io::harmony {

// Obtain sandbox paths from the OS rather than hard-coding el1/el2 or bundle names.
template <typename Getter>
std::string applicationDirectory(Getter getter) {
  std::array<char, PATH_MAX> buffer{};
  int32_t length = 0;
  const auto error = getter(buffer.data(), static_cast<int32_t>(buffer.size()), &length);
  if (error != ABILITY_RUNTIME_ERROR_CODE_NO_ERROR || length <= 0 ||
      static_cast<size_t>(length) >= buffer.size()) {
    throw std::runtime_error("Cannot obtain HarmonyOS application directory: " + std::to_string(error));
  }
  return std::string(buffer.data(), static_cast<size_t>(length));
}

inline std::string filesDir() {
  return applicationDirectory(OH_AbilityRuntime_ApplicationContextGetFilesDir);
}
inline std::string cacheDir() {
  return applicationDirectory(OH_AbilityRuntime_ApplicationContextGetCacheDir);
}
inline std::string tempDir() {
  return applicationDirectory(OH_AbilityRuntime_ApplicationContextGetTempDir);
}

} // namespace rct_io::harmony
