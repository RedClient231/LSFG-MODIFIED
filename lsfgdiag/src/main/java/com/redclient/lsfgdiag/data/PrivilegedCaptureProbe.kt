package com.redclient.lsfgdiag.data

import android.os.Build
import android.view.Display
import java.lang.reflect.Method
import java.lang.reflect.Modifier

/**
 * Mirrors the reflection chain in LSFG-Android's PrivilegedScreenCapture.kt
 * (lines 28-115) but stops BEFORE invoking captureDisplay(). We can't call it
 * from app UID anyway — that needs shell UID via Shizuku — but we can find
 * out whether each step in the reflection chain succeeds.
 *
 * If a step that succeeds in PrivilegedScreenCapture fails here on the user's
 * Helio G99 firmware, that's our explanation for "Shizuku doesn't work" —
 * because the Shizuku user service runs the same reflection chain in the
 * shell process, and reflection-blocked APIs are blocked for shell too.
 *
 * Output is line-oriented, formatted like the native probe's checklist so
 * the saved report stays readable.
 */
object PrivilegedCaptureProbe {

    fun runProbe(): String = buildString {
        append("=== Privileged screen-capture API probe ===\n")
        append("(replicates LSFG-Android's reflection chain so you can see what\n")
        append(" the Shizuku user service would face on your firmware)\n\n")

        append("Build: ${Build.MANUFACTURER} ${Build.MODEL} (${Build.DEVICE})\n")
        append("OS:    Android ${Build.VERSION.RELEASE} (SDK ${Build.VERSION.SDK_INT})\n")
        append("Brand: ${Build.BRAND} / ${Build.PRODUCT}\n")
        append("Build fingerprint: ${Build.FINGERPRINT}\n\n")

        // Match the same order LSFG tries: ScreenCapture first, SurfaceControl second.
        for (className in listOf(
            "android.window.ScreenCapture",
            "android.view.SurfaceControl",
        )) {
            append("--- backend: $className ---\n")
            probeBackend(className)
            append("\n")
        }

        append("--- display token resolution ---\n")
        probeDisplayTokenSources()
    }

    private fun StringBuilder.probeBackend(captureClassName: String) {
        val captureClass = runCatching { Class.forName(captureClassName) }.getOrNull()
        check("Class.forName($captureClassName)", captureClass != null)
        if (captureClass == null) return

        val builderClass = runCatching {
            Class.forName("$captureClassName\$DisplayCaptureArgs\$Builder")
        }.getOrNull()
        check("$captureClassName\$DisplayCaptureArgs\$Builder", builderClass != null)

        val argsClass = runCatching {
            Class.forName("$captureClassName\$DisplayCaptureArgs")
        }.getOrNull()
        check("$captureClassName\$DisplayCaptureArgs", argsClass != null)

        val screenshotClass = runCatching {
            Class.forName("$captureClassName\$ScreenshotHardwareBuffer")
        }.getOrNull()
        check("$captureClassName\$ScreenshotHardwareBuffer", screenshotClass != null)

        if (builderClass != null) {
            val ctors = builderClass.declaredConstructors
            val hasIBinderCtor = ctors.any { c ->
                c.parameterTypes.size == 1 && android.os.IBinder::class.java.isAssignableFrom(c.parameterTypes[0])
            }
            val hasIntCtor = ctors.any { c ->
                c.parameterTypes.size == 1 && c.parameterTypes[0] == Int::class.javaPrimitiveType
            }
            val hasNoArgCtor = ctors.any { c -> c.parameterTypes.isEmpty() }
            check("Builder(IBinder displayToken)", hasIBinderCtor)
            check("Builder(int displayId)", hasIntCtor)
            check("Builder()", hasNoArgCtor)

            // setUid is the critical one — that's the UID filter LSFG depends on.
            val setUidMethods = builderClass.methods.filter {
                it.name == "setUid" && it.parameterTypes.size == 1
            }
            val hasSetUidLong = setUidMethods.any { it.parameterTypes[0] == Long::class.javaPrimitiveType }
            val hasSetUidInt = setUidMethods.any { it.parameterTypes[0] == Int::class.javaPrimitiveType }
            check("Builder.setUid(long)", hasSetUidLong)
            check("Builder.setUid(int)",  hasSetUidInt)
            if (!hasSetUidLong && !hasSetUidInt) {
                append("    NOTE: this firmware lacks the UID filter on \$Builder.\n")
                append("    LSFG's Shizuku capture will fail at construct time.\n")
            }
        }

        if (captureClass != null && argsClass != null) {
            val captureDisplayMethod: Method? = (captureClass.methods.asSequence() + captureClass.declaredMethods.asSequence())
                .firstOrNull { m ->
                    m.name == "captureDisplay" &&
                        m.parameterTypes.size == 1 &&
                        m.parameterTypes[0].isAssignableFrom(argsClass)
                }
            check("$captureClassName.captureDisplay(DisplayCaptureArgs)", captureDisplayMethod != null)
            captureDisplayMethod?.let {
                val isStatic = Modifier.isStatic(it.modifiers)
                append("    captureDisplay isStatic=$isStatic returns=${it.returnType.simpleName}\n")
            }
        }

        if (screenshotClass != null) {
            val getHb = (screenshotClass.methods.asSequence() + screenshotClass.declaredMethods.asSequence())
                .firstOrNull { m -> m.name == "getHardwareBuffer" && m.parameterTypes.isEmpty() }
            check("ScreenshotHardwareBuffer.getHardwareBuffer()", getHb != null)
        }
    }

    private fun StringBuilder.probeDisplayTokenSources() {
        // 1) DisplayManagerGlobal.getDisplayToken(int)
        val dmgOk = runCatching {
            val dmgClass = Class.forName("android.hardware.display.DisplayManagerGlobal")
            val dmg = dmgClass.getMethod("getInstance").invoke(null)
                ?: return@runCatching false
            val m = dmgClass.methods.firstOrNull {
                it.name == "getDisplayToken" &&
                    it.parameterTypes.contentEquals(arrayOf(Int::class.javaPrimitiveType))
            } ?: return@runCatching false
            val token = m.invoke(dmg, Display.DEFAULT_DISPLAY) as? android.os.IBinder
            token != null
        }.getOrDefault(false)
        check("DisplayManagerGlobal.getDisplayToken(int)", dmgOk)

        // 2) IDisplayManager.getDisplayToken via ServiceManager
        val idmOk = runCatching {
            val sm = Class.forName("android.os.ServiceManager")
            val displayBinder = sm.getMethod("getService", String::class.java)
                .invoke(null, "display") as? android.os.IBinder
                ?: return@runCatching false
            val stub = Class.forName("android.hardware.display.IDisplayManager\$Stub")
            val proxy = stub.getMethod("asInterface", android.os.IBinder::class.java)
                .invoke(null, displayBinder)
                ?: return@runCatching false
            val m = proxy.javaClass.methods.firstOrNull {
                it.name == "getDisplayToken" &&
                    it.parameterTypes.contentEquals(arrayOf(Int::class.javaPrimitiveType))
            } ?: return@runCatching false
            val token = m.invoke(proxy, Display.DEFAULT_DISPLAY) as? android.os.IBinder
            token != null
        }.getOrDefault(false)
        check("IDisplayManager.getDisplayToken(int) via ServiceManager", idmOk)

        // 3) DisplayControl.getPhysicalDisplayIds + getPhysicalDisplayToken
        for (cls in listOf("android.view.DisplayControl", "android.view.SurfaceControl")) {
            val ok = runCatching {
                val c = Class.forName(cls)
                val ids = c.methods.firstOrNull { it.name == "getPhysicalDisplayIds" && it.parameterTypes.isEmpty() }
                    ?.invoke(null) as? LongArray
                    ?: return@runCatching false
                if (ids.isEmpty()) return@runCatching false
                val tokenMethod = c.methods.firstOrNull {
                    it.name == "getPhysicalDisplayToken" &&
                        it.parameterTypes.contentEquals(arrayOf(Long::class.javaPrimitiveType))
                } ?: return@runCatching false
                val token = tokenMethod.invoke(null, ids[0]) as? android.os.IBinder
                token != null
            }.getOrDefault(false)
            check("$cls.getPhysicalDisplayToken(long)", ok)
        }
    }

    private fun StringBuilder.check(label: String, ok: Boolean) {
        append("  [")
        append(if (ok) "Y" else "N")
        append("] ")
        append(label)
        append('\n')
    }
}
