#pragma once
#include <jsi/jsi.h>
#include <cmath>
#include <cctype>
#include <string_view>
#include <unordered_map>
#include "IOLimits.hpp"

namespace jsi_utils {
inline void validateSignature(facebook::jsi::Runtime& rt, std::string_view method,
                              const facebook::jsi::Value* args, size_t count,
                              std::string_view signature) {
  auto fail = [&](size_t index) {
    throw facebook::jsi::JSError(rt, std::string(method) + ": invalid argument at position " + std::to_string(index));
  };
  if (count > signature.size()) fail(signature.size());
  for (size_t i = 0; i < signature.size(); ++i) {
    char kind = signature[i];
    if (i >= count || args[i].isUndefined()) {
      if (std::islower(static_cast<unsigned char>(kind))) continue;
      fail(i);
    }
    kind = static_cast<char>(std::toupper(static_cast<unsigned char>(kind)));
    const auto& arg = args[i];
    switch (kind) {
      case 'P': case 'S': {
        if (!arg.isString()) fail(i);
        if (kind == 'P') rct_io::checkPath(arg.asString(rt).utf8(rt));
        break;
      }
      case 'B': if (!arg.isBool()) fail(i); break;
      case 'D':
        if (arg.isNull() || arg.isUndefined() || arg.isString()) break;
        [[fallthrough]];
      case 'A': {
        if (!arg.isObject() || !arg.asObject(rt).isArrayBuffer(rt)) fail(i);
        rct_io::checkBufferSize(arg.asObject(rt).getArrayBuffer(rt).size(rt));
        break;
      }
      case 'V': {
        if (!arg.isObject() || !arg.asObject(rt).isArray(rt)) fail(i);
        auto array = arg.asObject(rt).asArray(rt);
        if (array.size(rt) > 8192) fail(i);
        for (size_t j = 0; j < array.size(rt); ++j) if (!array.getValueAtIndex(rt, j).isString()) fail(i);
        break;
      }
      default: {
        if (!arg.isNumber()) fail(i);
        double number = arg.asNumber();
        double minimum = -9007199254740991.0, maximum = 9007199254740991.0;
        if (kind == 'H') { minimum = 1; maximum = 2147483647; }
        if (kind == 'M') { minimum = 0; maximum = 3; }
        if (kind == 'O') { minimum = 0; maximum = 5; }
        if (kind == 'K') { minimum = 0; maximum = 11; }
        if (kind == 'R') { minimum = 0; maximum = 2; }
        if (kind == 'Z') { minimum = -1; maximum = static_cast<double>(rct_io::maxBufferBytes); }
        if (kind == 'I') { minimum = 0; maximum = 2147483647; }
        if (kind == 'T') { minimum = 1; maximum = 2147483647; }
        if (kind == 'C') { minimum = 0; maximum = 8192; }
        if (!std::isfinite(number) || std::floor(number) != number || number < minimum || number > maximum) fail(i);
      }
    }
  }
}
inline void validateFSArguments(facebook::jsi::Runtime& rt, std::string name,
                               const facebook::jsi::Value* args, size_t count) {
  if (name.ends_with("Sync")) name.resize(name.size() - 4);
  if (name == "joinPaths") {
    for (size_t i = 0; i < count; ++i) validateSignature(rt, name, args + i, 1, "S");
    return;
  }
  static const std::unordered_map<std::string, std::string_view> signatures = {
    {"dispose",""},
    {"openFile","Po"}, {"fileClose","H"}, {"fileCloseAsync","H"}, {"fileSeek","HNr"},
    {"fileRewind","H"}, {"fileGetPosition","H"}, {"fileGetSize","H"}, {"fileIsEOF","H"},
    {"fileFlush","H"}, {"fileTruncate","H"}, {"fileRead","Hz"}, {"fileReadString","Hz"},
    {"fileReadLine","H"}, {"fileWrite","HA"}, {"fileWriteString","HS"}, {"fileWriteLine","HS"},
    {"writeString","PSmb"}, {"writeBytes","PAmb"}, {"copyFile","PPb"},
    {"moveFile","PP"}, {"moveDirectory","PP"}, {"createFile","Pb"},
    {"createDirectory","Pb"}, {"deleteDirectory","Pb"}, {"listDirectory","Pb"}, {"calcHash","Pk"}
  };
  auto it = signatures.find(name);
  validateSignature(rt, name, args, count, it == signatures.end() ? "P" : it->second);
}
} // namespace jsi_utils
