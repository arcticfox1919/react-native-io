import type resourceManager from '@ohos.resourceManager';
import { JSBundleProvider } from '@rnoh/react-native-openharmony';

// RNOH 0.84.3 maps raw resources at their HAP offsets. HBC requires alignment
// that the packager does not guarantee. Returning bytes makes RNOH copy them
// into its owned native buffer before Hermes executes the bundle.
export class BytecodeBundleProvider extends JSBundleProvider {
  private resources: resourceManager.ResourceManager;

  constructor(resources: resourceManager.ResourceManager) {
    super();
    this.resources = resources;
  }

  getURL(): string {
    return 'hermes_bundle.hbc';
  }

  getAppKeys(): string[] {
    return ['IoExample'];
  }

  async getBundle(): Promise<ArrayBuffer> {
    const bytes = await this.resources.getRawFileContent(this.getURL());
    if (bytes.byteOffset === 0 && bytes.byteLength === bytes.buffer.byteLength) {
      return bytes.buffer;
    }
    return bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
  }
}
