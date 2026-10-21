#include "NativeStdIO.h"
#include "ThreadPoolFactory.hpp"
#include "FSHostObject.hpp"
#include "IORequestHostObject.hpp"
#include "PlatformHostObject.hpp"

#include <string>

#ifdef __ANDROID__
#include "IOHttpClientAndroid.hpp"
#endif

namespace facebook::react {

NativeStdIO::NativeStdIO(std::shared_ptr<CallInvoker> jsInvoker)
  : NativeStdIOCxxSpec(std::move(jsInvoker)) {}

jsi::Object NativeStdIO::createFileSystem(jsi::Runtime &rt, double numThreads) {
  if (!std::isfinite(numThreads) || numThreads < 1 || numThreads > 32 || std::floor(numThreads) != numThreads)
    throw jsi::JSError(rt, "Thread count must be an integer between 1 and 32");
  auto threads = static_cast<unsigned int>(numThreads);
  // Sync-only callers do not need a worker. FSHostObject creates its pool on
  // the first async task and keeps it for the host object's remaining lifetime.
  auto hostObject = std::make_shared<rct_io::FSHostObject>(rt, threads, jsInvoker_);

  return jsi::Object::createFromHostObject(rt, hostObject);
}

jsi::Object NativeStdIO::createIORequest(jsi::Runtime &rt){
  // Network tasks are bounded process-wide instead of spawning a thread for
  // every convenience request. Each request keeps its own client/runtime state.
  // Keep the shared network workers alive for the process lifetime. Destroying
  // a static pool during C++ shutdown can otherwise wait behind an unresponsive
  // socket after the platform runtime has already begun tearing down.
  static const auto* threadPool = new std::shared_ptr<BS::thread_pool<>>(rct_io::makeThreadPool(4));
  auto hostObject = std::make_shared<rct_io::IORequestHostObject>(rt, *threadPool, jsInvoker_);
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
  rct_io::checkBufferSize(size);

  std::string encodingStr = encoding.utf8(rt);

  if (size == 0 && (encodingStr == "utf8" || encodingStr == "ascii" || encodingStr == "latin1"))
    return jsi::String::createFromUtf8(rt, "");
  if (encodingStr == "ascii" && std::any_of(data, data + size, [](unsigned char c) { return c > 127; }))
    throw jsi::JSError(rt, "ASCII decoding requires 7-bit input");
  if (encodingStr == "utf8") {
    return jsi::String::createFromUtf8(rt, std::string(data, size));
  } else if (encodingStr == "ascii") {
    return jsi::String::createFromAscii(rt, data, size);
  } else if (encodingStr == "latin1") {
    // Latin1: each byte (0x00-0xFF) maps to Unicode code point, encode as UTF-8
    std::string result;
    rct_io::checkBufferSize(size * 2);
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
    if (encodingStr == "ascii" && std::any_of(input.begin(), input.end(), [](unsigned char c) { return c > 127; }))
      throw jsi::JSError(rt, "ASCII encoding requires 7-bit input");
    rct_io::checkBufferSize(input.size());
    auto buffer = jsi_utils::toArrayBuffer(rt, std::vector<uint8_t>(input.begin(), input.end()));
    return std::move(buffer);
  } else if (encodingStr == "latin1") {
    // Latin1: decode UTF-8 to code points, keep 0x00-0xFF
    std::string input = str.utf8(rt);
    rct_io::checkBufferSize(input.size());
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
    rct_io::checkBufferSize(result.size());
    auto buffer = jsi_utils::toArrayBuffer(rt, std::vector<uint8_t>(result.begin(), result.end()));
    return std::move(buffer);
  }

  throw jsi::JSError(rt, "Unsupported encoding: " + encodingStr);
}

} // namespace facebook::react
