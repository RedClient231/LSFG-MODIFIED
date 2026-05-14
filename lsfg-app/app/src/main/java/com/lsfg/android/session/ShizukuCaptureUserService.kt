package com.lsfg.android.session

import android.hardware.HardwareBuffer
import android.os.SystemClock
import android.util.Log
import com.lsfg.android.shizuku.IShizukuCaptureService
import com.lsfg.android.shizuku.IShizukuFrameCallback
import java.util.concurrent.atomic.AtomicBoolean

class ShizukuCaptureUserService : IShizukuCaptureService.Stub() {

    private val running = AtomicBoolean(false)
    private var worker: Thread? = null

    override fun startCapture(
        targetUid: Int,
        width: Int,
        height: Int,
        maxFps: Int,
        callback: IShizukuFrameCallback,
    ) {
        stopCapture()
        val periodMs = (1000L / maxFps.coerceIn(15, 120)).coerceAtLeast(8L)
        running.set(true)
        worker = Thread({
            runCaptureLoop(targetUid, width, height, periodMs, callback)
        }, "lsfg-shizuku-capture").also { it.start() }
    }

    override fun stopCapture() {
        running.set(false)
        worker?.interrupt()
        worker = null
    }

    override fun describeBackend(): String {
        return "uid=${android.os.Process.myUid()} sdk=${android.os.Build.VERSION.SDK_INT}"
    }

    override fun destroy() {
        stopCapture()
        System.exit(0)
    }

    private fun runCaptureLoop(
        targetUid: Int,
        width: Int,
        height: Int,
        periodMs: Long,
        callback: IShizukuFrameCallback,
    ) {
        val capture = runCatching { PrivilegedScreenCapture(width, height, targetUid) }
            .getOrElse { e ->
                Log.w(TAG, "Unable to initialize privileged capture", e)
                callback.onError("Shizuku capture unavailable: ${e.message ?: e.javaClass.simpleName}")
                running.set(false)
                return
            }

        // ---- captureDisplay smoke test (transient + vendor-firmware-patch) -
        // Reflection chain resolving is necessary but not sufficient. Two
        // failure modes need disambiguating here:
        //
        //   1. Transient: the system's first capture call sometimes returns
        //      null while SurfaceFlinger is still warming up its capture
        //      session — observed on stock Pixel and Adreno builds. Recovers
        //      on the next attempt.
        //   2. Sticky vendor patch: MediaTek (HyperOS / OriginOS / many
        //      Helio G99 / Dimensity 700 firmwares), some Honor / Vivo
        //      builds, and a handful of MIUI revisions keep the
        //      captureDisplay symbol but patch its native implementation to
        //      return null when called from a non-system UID — even shell.
        //      No amount of retrying recovers this.
        //
        // Take up to three throwaway captures with a short backoff. If at
        // least one returns a buffer, treat the path as live and continue.
        // If all three return null, we surface a clear UI error pointing at
        // the firmware patch instead of silently spinning forever.
        var smoke: HardwareBuffer? = null
        var smokeAttempts = 0
        while (smokeAttempts < 3 && running.get()) {
            smokeAttempts++
            smoke = runCatching { capture.captureHardwareBuffer() }.getOrNull()
            if (smoke != null) break
            // 50 ms backoff is enough to let SurfaceFlinger settle without
            // adding noticeable startup latency on the happy path.
            try {
                Thread.sleep(50L)
            } catch (_: InterruptedException) {
                running.set(false)
                return
            }
        }
        if (smoke == null) {
            Log.w(TAG, "Smoke test: captureDisplay returned null on $smokeAttempts attempts — likely vendor firmware patch")
            callback.onError(
                "Shizuku capture is blocked on this firmware. The reflection chain " +
                "resolved (so the API is visible) but captureDisplay returned null " +
                "on $smokeAttempts attempts. This typically means a vendor (often " +
                "MediaTek) has patched SurfaceControl.captureDisplay to refuse " +
                "non-system callers. The app will continue using MediaProjection " +
                "for the visible video path; only the Shizuku timing side channel " +
                "is unavailable."
            )
            running.set(false)
            return
        }
        // Smoke test produced a frame — feed it through as the first frame
        // of the session so we don't waste it.
        run {
            val timestampNs = System.nanoTime()
            try {
                callback.onFrameMetrics(timestampNs, 0L, 0L)
                callback.onFrame(smoke, timestampNs)
            } catch (t: Throwable) {
                Log.w(TAG, "smoke-frame callback failed", t)
                running.set(false)
                return
            } finally {
                runCatching { smoke.close() }
            }
        }
        // ---- end MediaTek smoke test --------------------------------------

        var lastFrameNs = 0L
        val targetPeriodNs = periodMs * 1_000_000L
        var frameLogCount = 0
        while (running.get()) {
            val started = SystemClock.uptimeMillis()
            val hb = runCatching { capture.captureHardwareBuffer() }
                .onFailure {
                    Log.w(TAG, "captureHardwareBuffer failed", it)
                    callback.onError("Shizuku capture failed: ${it.message ?: it.javaClass.simpleName}")
                }
                .getOrNull()

            if (hb != null) {
                if (frameLogCount < 8) {
                    frameLogCount++
                    Log.i(TAG, "captured frame #$frameLogCount uid=$targetUid ${hb.width}x${hb.height} fmt=${hb.format}")
                }
                val timestampNs = System.nanoTime()
                val frameTimeNs = if (lastFrameNs > 0L) timestampNs - lastFrameNs else 0L
                val pacingJitterNs = if (frameTimeNs > 0L) kotlin.math.abs(frameTimeNs - targetPeriodNs) else 0L
                lastFrameNs = timestampNs
                try {
                    callback.onFrameMetrics(timestampNs, frameTimeNs, pacingJitterNs)
                    callback.onFrame(hb, timestampNs)
                } catch (t: Throwable) {
                    Log.w(TAG, "frame callback failed", t)
                    running.set(false)
                } finally {
                    runCatching { hb.close() }
                }
            }

            val elapsed = SystemClock.uptimeMillis() - started
            val sleepMs = periodMs - elapsed
            if (sleepMs > 0) {
                try {
                    Thread.sleep(sleepMs)
                } catch (_: InterruptedException) {
                    break
                }
            }
        }
    }


    companion object {
        private const val TAG = "ShizukuUserCapture"
    }
}
