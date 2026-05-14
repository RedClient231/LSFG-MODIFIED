package com.redclient.lsfgdiag.data

import android.os.Build
import java.io.BufferedReader
import java.io.InputStreamReader

/**
 * Captures recent logcat output. On user builds (no READ_LOGS permission),
 * `logcat -d` returns ONLY the calling app's own log entries — that's a
 * deliberate Android privilege model. So this is most useful when:
 *
 *   1. The user has just had LSFG-Android crash/glitch on this device
 *      (LSFG's logs go to the *system* buffer, which we cannot read), OR
 *   2. The user has connected ADB and granted `pm grant
 *      com.redclient.lsfgdiag android.permission.READ_LOGS`, in which case
 *      logcat returns the system-wide buffer.
 *
 * We still always try — the report explains the empty case clearly.
 */
object LogcatGrabber {

    /**
     * Tag-prefixes worth filtering for when looking at LSFG-Android issues.
     * Matches what LSFG's NativeBridge / cpp logging actually emits.
     */
    private val TAG_FILTERS = listOf(
        "lsfg-vk",          // native side: probe, session, ahb, render_loop, jni
        "lsfg-android",
        "LSFG",
        "Vulkan",           // Android Vulkan loader chatter
        "VULKAN",
        "ScreenCapture",
        "SurfaceControl",   // surfaces this if the reflection layer trips
        "PrivilegedCapture",
        "ShizukuCapture",
        "ShizukuUserCapture",
        "DEBUG",            // tombstone-style native crashes
        "libc",
        "AndroidRuntime",   // Java exceptions
    )

    /**
     * Returns up to `maxLines` of recent logcat output. The header notes
     * whether READ_LOGS appears to be granted.
     */
    fun grab(maxLines: Int = 5000): String {
        val sb = StringBuilder(64 * 1024)
        sb.append("=== Logcat (last ").append(maxLines).append(" lines) ===\n")
        sb.append("Note: on user builds without READ_LOGS, this only contains\n")
        sb.append("THIS app's own log entries. To capture LSFG-Android's logs,\n")
        sb.append("connect ADB and run:\n")
        sb.append("  adb shell pm grant com.redclient.lsfgdiag android.permission.READ_LOGS\n")
        sb.append("then re-open this app and tap Save report again.\n\n")

        val cmd = arrayOf(
            "logcat",
            "-d",                       // dump and exit
            "-t", maxLines.toString(),  // last N lines
            "-v", "threadtime",         // include pid/tid/level
        )

        val proc: Process = try {
            ProcessBuilder(*cmd)
                .redirectErrorStream(true)
                .start()
        } catch (t: Throwable) {
            sb.append("logcat exec failed: ${t.message}\n")
            return sb.toString()
        }

        val totalLines: Int
        BufferedReader(InputStreamReader(proc.inputStream)).use { r ->
            var count = 0
            while (true) {
                val line = r.readLine() ?: break
                count++
                // We DON'T pre-filter — the user benefits from seeing all
                // recent lines, but we tag the obviously-relevant ones with a
                // marker so they're easier to find when scrolling.
                val mark = if (TAG_FILTERS.any { line.contains(it, ignoreCase = false) }) ">>> " else "    "
                sb.append(mark).append(line).append('\n')
            }
            totalLines = count
        }
        runCatching { proc.waitFor() }

        if (totalLines == 0) {
            sb.append("(logcat returned 0 lines — READ_LOGS not granted, and this app has\n")
            sb.append(" not yet emitted anything to its own buffer.)\n")
        } else {
            sb.append("\n=== Logcat captured: ").append(totalLines).append(" line(s) ===\n")
        }
        return sb.toString()
    }
}
