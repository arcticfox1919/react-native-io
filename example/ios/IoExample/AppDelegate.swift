import UIKit
import React
import React_RCTAppDelegate
import ReactAppDependencyProvider

@main
class AppDelegate: UIResponder, UIApplicationDelegate {
  var window: UIWindow?

  var reactNativeDelegate: ReactNativeDelegate?
  var reactNativeFactory: RCTReactNativeFactory?

  func application(
    _ application: UIApplication,
    didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]? = nil
  ) -> Bool {
    NSLog("IO_QUALITY_NATIVE application bootstrap")
    NotificationCenter.default.addObserver(forName: UserDefaults.didChangeNotification, object: nil, queue: .main) { _ in
      if let progress = UserDefaults.standard.string(forKey: "IOQualityProgress") {
        NSLog("%@", progress)
      }
    }
    if let path = Bundle.main.path(forResource: "ip", ofType: "txt"),
       let host = try? String(contentsOfFile: path, encoding: .utf8) {
      UserDefaults.standard.set(host.trimmingCharacters(in: .whitespacesAndNewlines), forKey: "IOQualityMetroHost")
    }
    let delegate = ReactNativeDelegate()
    let factory = RCTReactNativeFactory(delegate: delegate)
    delegate.dependencyProvider = RCTAppDependencyProvider()

    reactNativeDelegate = delegate
    reactNativeFactory = factory

    window = UIWindow(frame: UIScreen.main.bounds)

    factory.startReactNative(
      withModuleName: "IoExample",
      in: window,
      launchOptions: launchOptions
    )

    return true
  }
}

class ReactNativeDelegate: RCTDefaultReactNativeFactoryDelegate {
  override func sourceURL(for bridge: RCTBridge) -> URL? {
    self.bundleURL()
  }

  override func bundleURL() -> URL? {
#if IO_EXAMPLE_BUNDLED || IO_QUALITY_BUNDLED
    NSLog("IO_EXAMPLE_NATIVE compiled embedded entry")
    return Bundle.main.url(forResource: "main", withExtension: "jsbundle")
#else
    // Device laboratory entry can use an embedded bundle without a Metro server.
    if ProcessInfo.processInfo.arguments.contains("--io-quality-bundled") {
      let embedded = Bundle.main.url(forResource: "main", withExtension: "jsbundle")
      NSLog("IO_QUALITY_NATIVE embedded bundle found=%@", embedded == nil ? "false" : "true")
      return embedded
    }
#if DEBUG
    return RCTBundleURLProvider.sharedSettings().jsBundleURL(forBundleRoot: "index")
#else
    return Bundle.main.url(forResource: "main", withExtension: "jsbundle")
#endif
#endif
  }
}
