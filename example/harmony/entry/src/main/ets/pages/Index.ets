import {
  AnyJSBundleProvider,
  MetroJSBundleProvider,
  RNApp,
  RNOHErrorDialog,
  RNOHCoreContext
} from '@rnoh/react-native-openharmony';
import { getRNOHPackages } from '../PackageProvider';
import { BytecodeBundleProvider } from '../BytecodeBundleProvider';

@Entry
@Component
struct Index {
  @StorageLink('RNOHCoreContext') private rnohCoreContext: RNOHCoreContext | undefined = undefined

  build() {
    Column() {
      if (this.rnohCoreContext) {
        if (this.rnohCoreContext?.isDebugModeEnabled) {
          RNOHErrorDialog({ ctx: this.rnohCoreContext })
        }
        RNApp({
          rnInstanceConfig: {
            name: "IoExample",
            createRNPackages: getRNOHPackages,
            fontResourceByFontFamily: {},
            enableDebugger: this.rnohCoreContext?.isDebugModeEnabled,
          },
          appKey: "IoExample",
          jsBundleProvider: this.rnohCoreContext?.isDebugModeEnabled ?
            new AnyJSBundleProvider([
              new MetroJSBundleProvider(),
              new BytecodeBundleProvider(this.rnohCoreContext.uiAbilityContext.resourceManager),
            ]) :
            /**
             * NOTE: The HBC format is recommended for Hermes in a production environment. HBC provides better performance and loading times.
             */
            new BytecodeBundleProvider(this.rnohCoreContext.uiAbilityContext.resourceManager),
        })
      }
    }
    .height('100%')
    .width('100%')
  }
}
