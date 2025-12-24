#include "NativeStdIO.h"
#include "FSHostObject.hpp"
#include "IORequestHostObject.hpp"
#include "PlatformHostObject.hpp"

#include <string>

#ifdef __ANDROID__
#include "IOHttpClientAndroid.hpp"
#endif

namespace facebook::react {

namespace {
  // Helper to create ArrayBuffer via JavaScript constructor (for cross-platform compatibility)
  jsi::ArrayBuffer createArrayBuffer(jsi::Runtime &rt, size_t size) {
    auto arrayBufferCtor = rt.global().getPropertyAsFunction(rt, "ArrayBuffer");
    return arrayBufferCtor.callAsConstructor(rt, static_cast<int>(size))
        .asObject(rt)
        .getArrayBuffer(rt);
  }
} // anonymous namespace

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

jsi::String NativeStdIO::decodeString(jsi::Runtime &rt, jsi::Object buffer, jsi::String encoding) {
  if (!buffer.isArrayBuffer(rt)) {
    throw jsi::JSError(rt, "decodeString: buffer must be an ArrayBuffer");
  }

  auto arrayBuffer = buffer.getArrayBuffer(rt);
  auto data = reinterpret_cast<const char*>(arrayBuffer.data(rt));
  auto size = arrayBuffer.size(rt);

  std::string encodingStr = encoding.utf8(rt);

  if (encodingStr == "utf8") {
    return jsi::String::createFromUtf8(rt, std::string(data, size));
  } else if (encodingStr == "ascii") {
    return jsi::String::createFromAscii(rt, data, size);
  } else if (encodingStr == "latin1") {
    // Latin1: each byte (0x00-0xFF) maps to Unicode code point, encode as UTF-8
    std::string result;
    result.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
      unsigned char c = static_cast<unsigned char>(data[i]);
      if (c < 0x80) {
        result.push_back(static_cast<char>(c));
      } else {
        result.push_back(static_cast<char>(0xC0 | (c >> 6)));
        result.push_back(static_cast<char>(0x80 | (c & 0x3F)));
      }
    }
    return jsi::String::createFromUtf8(rt, result);
  }

  throw jsi::JSError(rt, "Unsupported encoding: " + encodingStr);
}

jsi::Object NativeStdIO::encodeString(jsi::Runtime &rt, jsi::String str, jsi::String encoding) {
  std::string encodingStr = encoding.utf8(rt);

  if (encodingStr == "utf8" || encodingStr == "ascii") {
    // UTF-8 and ASCII share the same byte representation for 0x00-0x7F
    std::string input = str.utf8(rt);
    auto buffer = createArrayBuffer(rt, input.size());
    memcpy(buffer.data(rt), input.data(), input.size());
    return std::move(buffer);
  } else if (encodingStr == "latin1") {
    // Latin1: decode UTF-8 to code points, keep 0x00-0xFF
    std::string input = str.utf8(rt);
    std::string result;
    result.reserve(input.size());
    for (size_t i = 0; i < input.size(); ) {
      unsigned char c = static_cast<unsigned char>(input[i]);
      uint32_t codePoint;
      if (c < 0x80) {
        codePoint = c;
        ++i;
      } else if ((c & 0xE0) == 0xC0 && i + 1 < input.size()) {
        codePoint = ((c & 0x1F) << 6) | (input[i + 1] & 0x3F);
        i += 2;
      } else if ((c & 0xF0) == 0xE0 && i + 2 < input.size()) {
        codePoint = ((c & 0x0F) << 12) | ((input[i + 1] & 0x3F) << 6) | (input[i + 2] & 0x3F);
        i += 3;
      } else if ((c & 0xF8) == 0xF0 && i + 3 < input.size()) {
        codePoint = ((c & 0x07) << 18) | ((input[i + 1] & 0x3F) << 12) |
                    ((input[i + 2] & 0x3F) << 6) | (input[i + 3] & 0x3F);
        i += 4;
      } else {
        ++i;
        continue;
      }
      result.push_back(codePoint <= 0xFF ? static_cast<char>(codePoint) : '?');
    }
    auto buffer = createArrayBuffer(rt, result.size());
    memcpy(buffer.data(rt), result.data(), result.size());
    return std::move(buffer);
  }

  throw jsi::JSError(rt, "Unsupported encoding: " + encodingStr);
}

} // namespace facebook::react
