/** Native launch metadata keeps APK/native mode separate from JS __DEV__. */
export interface DeviceLaunchOptions {
  ioExampleAction?: 'quality' | 'benchmark';
  ioBenchmarkRunId?: string;
  ioExampleBuild?: {
    type: string;
    debuggable: boolean;
    minified: boolean;
    variant: string;
  };
}
