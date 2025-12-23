import { TurboModuleRegistry, type TurboModule } from 'react-native';
import type { IOFileSystem, IORequest, IOPlatform } from './types';

/**
 * @internal TurboModule Spec (not exported)
 */
interface Spec extends TurboModule {
  createFileSystem(numThreads: number): Object;
  createIORequest(): Object;
  createPlatform(): Object;
  installHttpClient(): void;
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
