package io.example

import android.os.Bundle
import com.facebook.react.ReactActivity
import com.facebook.react.ReactActivityDelegate
import com.facebook.react.defaults.DefaultNewArchitectureEntryPoint.fabricEnabled
import com.facebook.react.defaults.DefaultReactActivityDelegate

class MainActivity : ReactActivity() {

  /**
   * Returns the name of the main component registered from JavaScript. This is used to schedule
   * rendering of the component.
   */
  override fun getMainComponentName(): String = "IoExample"

  /**
   * Returns the instance of the [ReactActivityDelegate]. We use [DefaultReactActivityDelegate]
   * which allows you to enable New Architecture with a single boolean flags [fabricEnabled]
   */
  override fun createReactActivityDelegate(): ReactActivityDelegate =
      object : DefaultReactActivityDelegate(this, mainComponentName, fabricEnabled) {
        override fun getLaunchOptions(): Bundle = Bundle().apply {
          val action = intent?.getStringExtra("ioExampleAction")
          if (action == "benchmark" || action == "quality") {
            putString("ioExampleAction", action)
          }
          val runId = intent?.getStringExtra("ioBenchmarkRunId")
          if (runId != null && runId.matches(Regex("[A-Za-z0-9._-]{1,64}"))) {
            putString("ioBenchmarkRunId", runId)
          }
          putBundle("ioExampleBuild", Bundle().apply {
            putString("type", BuildConfig.BUILD_TYPE)
            putBoolean("debuggable", BuildConfig.DEBUG)
            putBoolean("minified", BuildConfig.IO_EXAMPLE_R8)
            putString("variant", BuildConfig.IO_EXAMPLE_VARIANT)
          })
        }
      }
}
