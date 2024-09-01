package org.dolphinemu.dolphinemu.utils

import android.app.Activity
import android.app.Application.ActivityLifecycleCallbacks
import android.os.Bundle

class ActivityTracker : ActivityLifecycleCallbacks {
    val resumedActivities = HashSet<Activity>()
    var backgroundExecutionAllowed = false
    var currentActivity : Activity? = null
        private set

    override fun onActivityCreated(activity: Activity, bundle: Bundle?) {}

    override fun onActivityStarted(activity: Activity) {}

    override fun onActivityResumed(activity: Activity) {
        currentActivity = activity
        resumedActivities.add(activity)
        if (!backgroundExecutionAllowed && !resumedActivities.isEmpty()) {
            backgroundExecutionAllowed = true
            setBackgroundExecutionAllowedNative(true)
        }
    }

    override fun onActivityPaused(activity: Activity) {
        if (currentActivity === activity) {
            currentActivity = null
        }
        resumedActivities.remove(activity)
        if (backgroundExecutionAllowed && resumedActivities.isEmpty()) {
            backgroundExecutionAllowed = false
            setBackgroundExecutionAllowedNative(false)
        }
    }

    override fun onActivityStopped(activity: Activity) {}

    override fun onActivitySaveInstanceState(activity: Activity, bundle: Bundle) {}

    override fun onActivityDestroyed(activity: Activity) {}

    companion object {
        @JvmStatic
        external fun setBackgroundExecutionAllowedNative(allowed: Boolean)
    }
}
