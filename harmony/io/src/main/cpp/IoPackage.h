#pragma once

#include "RNOH/Package.h"
#include "NativeStdIO.h"

namespace rnoh {

class IoTurboModuleFactoryDelegate final : public TurboModuleFactoryDelegate {
 public:
  SharedTurboModule createTurboModule(Context ctx, const std::string& name) const override {
    if (name == facebook::react::NativeStdIO::kModuleName) {
      return std::make_shared<facebook::react::NativeStdIO>(ctx.jsInvoker);
    }
    return nullptr;
  }
};

class IoPackage final : public Package {
 public:
  explicit IoPackage(Package::Context ctx) : Package(ctx) {}

  std::unique_ptr<TurboModuleFactoryDelegate> createTurboModuleFactoryDelegate() override {
    return std::make_unique<IoTurboModuleFactoryDelegate>();
  }
};

} // namespace rnoh
