package com.redclient.lsfgdiag.data

/**
 * Thin JNI bridge to libsfgdiag.so. The native side does all the work and
 * returns one UTF-8 string. See app/src/main/cpp/vk_probe.cpp.
 */
object VulkanProbe {

    init {
        System.loadLibrary("lsfgdiag")
    }

    @JvmStatic
    private external fun runProbeNative(): String

    /**
     * Run a fast, side-effect-free Vulkan capability probe.
     * Wraps any unexpected throwable into a human-readable error block so the
     * report-saving code never has to handle exceptions from this layer.
     */
    fun runProbe(): String = try {
        runProbeNative()
    } catch (t: Throwable) {
        buildString {
            append("=== Vulkan probe ===\n")
            append("FAILED: ${t.javaClass.simpleName}\n")
            append("message: ${t.message ?: "<null>"}\n")
            append("\nThis usually means libvulkan.so could not be loaded or the\n")
            append("native probe library failed to link. Either points at a\n")
            append("broken Vulkan stack on this firmware.\n")
        }
    }
}
