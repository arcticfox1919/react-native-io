import type { Profile } from './engine';

/**
 * Only set this for a separate low-limit test build compiled with matching
 * RCT_IO_MAX_BUFFER_BYTES (e.g. 65536) on BOTH native platforms.
 * The runner refuses limits above 1 MiB, so default builds never allocate 256 MiB
 * merely to prove limit+1 rejection. Leave undefined for production/default builds.
 */
export const TEST_NATIVE_BUFFER_LIMIT_BYTES: number | undefined = undefined;
/** Optional local fixture URL for automated Harness network cases. */
export const TEST_HTTP_BASE_URL = '';

/** UI profiles are independent; set this to full/soak for automated Harness runs. */
export const HARNESS_PROFILE: Profile = 'quick';

/** Standalone automatic entry runs full coverage without a Metro test bridge. */
export const AUTO_PROFILE: Profile = 'full';
