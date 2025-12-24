#include "NativeStdIO.h"
#include "FSHostObject.hpp"
#include "IORequestHostObject.hpp"
#include "PlatformHostObject.hpp"

#ifdef __ANDROID__
#include "IOHttpClientAndroid.hpp"
#endif

namespace facebook::react {

NativeStdIO::NativeStdIO(std::shared_ptr<CallInvoker> jsInvoker)
  : NativeStdIOCxxSpec(std::move(jsInvoker)) {}

jsi::Object NativeStdIO::createFileSystem(jsi::Runtime &rt, double numThreads) {
  auto threads = static_cast<unsigned int>(numThreads > 0 ? numThreads : 3);
  auto threadPool = std::make_shared<BS::thread_pool<>>(threads);
  auto hostObject = std::make_shared<rct_io::FSHostObject>(rt, std::move(threadPool), jsInvoker_);

  return jsi::Object::createFromHostObject(rt, hostObject);
}

jsi::Object NativeStdIO::createIORequest(jsi::Runtime &rt){
  auto threadPool = std::make_shared<BS::thread_pool<>>(1);
  auto hostObject = std::make_shared<rct_io::IORequestHostObject>(rt, std::move(threadPool), jsInvoker_);
  return jsi::Object::createFromHostObject(rt, hostObject);
}

jsi::Object NativeStdIO::createPlatform(jsi::Runtime &rt){
  auto hostObject = std::make_shared<rct_io::PlatformHostObject>(rt);
  return jsi::Object::createFromHostObject(rt, hostObject);
}

void NativeStdIO::installHttpClient(jsi::Runtime &/*rt*/) {
#ifdef __ANDROID__
  // Pre-warm fbjni class caches on Android
  // This is called from JS to initialize HTTP client classes
  rct_io::network::installHttpClientCaches();
#endif
  // No-op on iOS - class caches are not needed
}

} // namespace facebook::react
