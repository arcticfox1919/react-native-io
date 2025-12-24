#pragma once

#include "StdIOSpecJSI.h"

namespace facebook::react  {

class NativeStdIO : public NativeStdIOCxxSpec<NativeStdIO> {
public:
  NativeStdIO(std::shared_ptr<CallInvoker> jsInvoker);

  jsi::Object createFileSystem(jsi::Runtime &rt, double numThreads);

  jsi::Object createIORequest(jsi::Runtime &rt);

  jsi::Object createPlatform(jsi::Runtime &rt);

  void installHttpClient(jsi::Runtime &rt);

  // String encoding/decoding
  jsi::String decodeString(jsi::Runtime &rt, jsi::Object buffer, jsi::String encoding);
  jsi::Object encodeString(jsi::Runtime &rt, jsi::String str, jsi::String encoding);

  static constexpr const char* kModuleName = "NativeStdIO";
};

} // namespace facebook::react
