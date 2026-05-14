package com.redclient.lsfgdiag.data

import android.app.ActivityManager
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Environment
import android.os.StatFs
import android.util.DisplayMetrics
import android.view.WindowManager
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone

/**
 * Collects all the device/build/runtime context that's useful when triaging
 * a frame-gen issue but doesn't fit cleanly under the Vulkan or capture-API
 * probes. Pure JVM, no native, no permissions.
 */
object DeviceInfoCollector {

    fun collect(ctx: Context): String = buildString {
        append("=== Device info ===\n")
        append("Generated:   ").append(timestamp()).append('\n')
        append("Manufacturer:").append(' ').append(Build.MANUFACTURER).append('\n')
        append("Brand:       ").append(Build.BRAND).append('\n')
        append("Model:       ").append(Build.MODEL).append('\n')
        append("Device:      ").append(Build.DEVICE).append('\n')
        append("Product:     ").append(Build.PRODUCT).append('\n')
        append("Hardware:    ").append(Build.HARDWARE).append('\n')      // SoC family hint
        append("Board:       ").append(Build.BOARD).append('\n')
        append("Bootloader:  ").append(Build.BOOTLOADER).append('\n')
        append("ABIs:        ").append(Build.SUPPORTED_ABIS.joinToString()).append('\n')
        append("Android:     ").append(Build.VERSION.RELEASE)
            .append(" (SDK ").append(Build.VERSION.SDK_INT).append(", ")
            .append(Build.VERSION.INCREMENTAL).append(")\n")
        append("Build type:  ").append(Build.TYPE).append('\n')
        append("Tags:        ").append(Build.TAGS).append('\n')
        append("Fingerprint: ").append(Build.FINGERPRINT).append('\n')
        append("Display:     ").append(Build.DISPLAY).append('\n')

        // SoC info — added in API 31, gives "MT8781V" style values that map to Helio G99.
        val socManufacturer = runCatching { Build.SOC_MANUFACTURER }.getOrDefault("?")
        val socModel = runCatching { Build.SOC_MODEL }.getOrDefault("?")
        append("SoC:         ").append(socManufacturer).append(' ').append(socModel).append('\n')

        // ---- Display / window ----
        val wm = ctx.getSystemService(Context.WINDOW_SERVICE) as WindowManager
        val display = wm.defaultDisplay
        val dm = DisplayMetrics()
        @Suppress("DEPRECATION")
        display.getRealMetrics(dm)
        append("\n=== Display ===\n")
        append("Resolution:  ").append(dm.widthPixels).append('x').append(dm.heightPixels).append('\n')
        append("Density:     ").append(dm.densityDpi).append(" dpi\n")
        append("Refresh:     ").append(String.format(Locale.US, "%.2f Hz", display.refreshRate)).append('\n')
        runCatching {
            // supportedModes is API 23+, present on all our targets.
            display.supportedModes.forEach { mode ->
                append("  mode ${mode.modeId}: ${mode.physicalWidth}x${mode.physicalHeight} @ ${"%.2f".format(mode.refreshRate)}Hz\n")
            }
        }

        // ---- Memory ----
        val am = ctx.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
        val mi = ActivityManager.MemoryInfo().also { am.getMemoryInfo(it) }
        append("\n=== Memory ===\n")
        append("Total RAM:   ").append(mi.totalMem / (1024 * 1024)).append(" MiB\n")
        append("Avail RAM:   ").append(mi.availMem / (1024 * 1024)).append(" MiB\n")
        append("Low mem:     ").append(mi.lowMemory).append('\n')

        // ---- Storage ----
        val data = StatFs(Environment.getDataDirectory().path)
        val totalGib = (data.blockCountLong * data.blockSizeLong) / (1024.0 * 1024.0 * 1024.0)
        val availGib = (data.availableBlocksLong * data.blockSizeLong) / (1024.0 * 1024.0 * 1024.0)
        append("\n=== Storage (/data) ===\n")
        append("Total:       ").append(String.format(Locale.US, "%.1f GiB", totalGib)).append('\n')
        append("Available:   ").append(String.format(Locale.US, "%.1f GiB", availGib)).append('\n')

        // ---- Vulkan availability flag ----
        val pm = ctx.packageManager
        append("\n=== Feature flags ===\n")
        append("Vulkan level support: ")
            .append(pm.hasSystemFeature(PackageManager.FEATURE_VULKAN_HARDWARE_LEVEL))
            .append('\n')
        append("Vulkan version:       ")
            .append(pm.hasSystemFeature(PackageManager.FEATURE_VULKAN_HARDWARE_VERSION))
            .append('\n')
        append("Compute shaders:      ")
            .append(pm.hasSystemFeature(PackageManager.FEATURE_VULKAN_HARDWARE_COMPUTE))
            .append('\n')
        append("Game pad:             ")
            .append(pm.hasSystemFeature(PackageManager.FEATURE_GAMEPAD))
            .append('\n')
    }

    private fun timestamp(): String {
        val fmt = SimpleDateFormat("yyyy-MM-dd HH:mm:ss z", Locale.US)
        fmt.timeZone = TimeZone.getDefault()
        return fmt.format(Date())
    }
}
