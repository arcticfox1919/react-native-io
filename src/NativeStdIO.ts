import { TurboModuleRegistry, type TurboModule } from 'react-native';
import type { IOFileSystem, IORequest, IOPlatform } from './types';

/**
 * String encoding types supported by native layer
 */
export type StringEncoding = 'utf8' | 'ascii' | 'latin1';

/**
 * @internal TurboModule Spec (not exported)
 */
interface Spec extends TurboModule {
  createFileSystem(numThreads: number): Object;
  createIORequest(): Object;
  createPlatform(): Object;
  installHttpClient(): void;
  // String encoding/decoding (Object is used because Codegen doesn't support ArrayBuffer)
  decodeString(buffer: Object, encoding: string): string;
  encodeString(str: string, encoding: string): Object;
}

const NativeModule = TurboModuleRegistry.getEnforcing<Spec>('NativeStdIO');

/**
 * Create a new IOFileSystem instance.
 * Each call creates a new native file system client with its own thread pool.
 *
 * @param numThreads Number of worker threads for async operations (default: 1)
 * @returns New IOFileSystem instance
 *
 * @internal This function is for internal use. Use FSContext/openFS() for
 *           configurable thread pools, or File/Directory classes directly.
 */
export function createFileSystem(numThreads: number = 1): IOFileSystem {
  return NativeModule.createFileSystem(numThreads) as IOFileSystem;
}

/**
 * Create a new IORequest instance.
 * Each call creates a new native HTTP client with its own thread pool.
 *
 * @returns New IORequest instance
 */
export function createRequest(): IORequest {
  return NativeModule.createIORequest() as IORequest;
}

/**
 * Create a new IOPlatform instance.
 * Provides platform-specific directory paths.
 *
 * @returns New IOPlatform instance
 */
export function createPlatform(): IOPlatform {
  return NativeModule.createPlatform() as IOPlatform;
}

/**
 * Install HTTP client (Android only).
 * Pre-warms JNI class caches for better performance.
 * This should be called before using HTTP functionality on Android.
 * On iOS, this is a no-op.
 *
 * @internal This function is for internal use.
 */
export function installHttpClient(): void {
  NativeModule.installHttpClient();
}

/**
 * Decode binary data to string using native implementation.
 * Much faster than JavaScript implementation for large buffers.
 *
 * @param buffer Binary data to decode
 * @param encoding String encoding (default: 'utf8')
 * @returns Decoded string
 *
 * @example
 * ```typescript
 * const buffer = new ArrayBuffer(5);
 * const view = new Uint8Array(buffer);
 * view.set([72, 101, 108, 108, 111]); // "Hello"
 * const str = decodeString(buffer); // "Hello"
 * ```
 */
export function decodeString(
  buffer: ArrayBuffer,
  encoding: StringEncoding = 'utf8'
): string {
  return NativeModule.decodeString(buffer as Object, encoding);
}

/**
 * Encode string to binary data using native implementation.
 *
 * @param str String to encode
 * @param encoding String encoding (default: 'utf8')
 * @returns Encoded binary data
 *
 * @example
 * ```typescript
 * const buffer = encodeString("Hello");
 * const view = new Uint8Array(buffer); // [72, 101, 108, 108, 111]
 * ```
 */
export function encodeString(
  str: string,
  encoding: StringEncoding = 'utf8'
): ArrayBuffer {
  return NativeModule.encodeString(str, encoding) as ArrayBuffer;
}
