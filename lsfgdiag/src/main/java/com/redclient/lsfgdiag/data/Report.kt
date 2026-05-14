package com.redclient.lsfgdiag.data

import android.content.Context
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone

/**
 * Domain model for one diagnostic report. Each section is independently
 * computed so the UI can show partial progress while later sections still run.
 */
data class Report(
    val deviceInfo: String,
    val vulkanProbe: String,
    val captureProbe: String,
    val logcat: String,
) {
    /** Concatenates every section into a single shareable plain-text document. */
    fun toPlainText(): String = buildString {
        append("LsfgDiag report — please attach this file when reporting an LSFG-Android issue.\n")
        append("================================================================\n\n")
        append(deviceInfo).append("\n\n")
        append(vulkanProbe).append("\n\n")
        append(captureProbe).append("\n\n")
        append(logcat).append('\n')
    }

    /** Stable, sortable filename including a timestamp and the device model. */
    fun suggestedFilename(): String {
        val ts = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).apply {
            timeZone = TimeZone.getDefault()
        }.format(Date())
        val safeModel = android.os.Build.MODEL
            .replace(Regex("[^A-Za-z0-9._-]"), "_")
            .take(32)
        return "lsfgdiag-$safeModel-$ts.txt"
    }

    companion object {
        /**
         * Builds a fresh report. Heavy work — runs the native Vulkan probe,
         * the reflection probe, and shells out to logcat. Caller must invoke
         * from a background dispatcher.
         */
        fun build(ctx: Context): Report = Report(
            deviceInfo  = DeviceInfoCollector.collect(ctx),
            vulkanProbe = VulkanProbe.runProbe(),
            captureProbe = PrivilegedCaptureProbe.runProbe(),
            logcat      = LogcatGrabber.grab(maxLines = 5000),
        )
    }
}
