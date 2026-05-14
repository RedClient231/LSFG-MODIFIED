// Vulkan capability probe.
//
// Mirrors exactly the queries LSFG-Android's android_vk_session.cpp performs
// at startup, so the report tells us *in advance* whether your phone's driver
// would accept LSFG's session creation.
//
// We deliberately do not create a device or load shaders here — the goal is a
// fast, side-effect-free read of static driver advertised features.

#include <jni.h>
#include <android/log.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>  // VK_KHR_android_surface, VK_ANDROID_external_memory_android_hardware_buffer

#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

#define LOG_TAG "lsfgdiag"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)

namespace {

// Extensions LSFG-Android requires at the device level (from
// android_vk_session.cpp:24-34). If any of these are missing the LSFG session
// will fail with kSessionMissingExtension and the app refuses to run.
constexpr const char *kRequiredDeviceExt[] = {
    VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
    VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
    VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME,
    VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME,
    VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
    VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
    VK_KHR_BIND_MEMORY_2_EXTENSION_NAME,
    VK_KHR_MAINTENANCE1_EXTENSION_NAME,
};

// Optional but informative — these gate fast paths in LSFG.
constexpr const char *kOptionalDeviceExt[] = {
    VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
    VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME,
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    "VK_KHR_timeline_semaphore",
    "VK_KHR_synchronization2",
    "VK_KHR_vulkan_memory_model",
    "VK_KHR_shader_float16_int8",
};

// Instance extensions checked separately (the WSI ones).
constexpr const char *kInstanceExt[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
};

bool hasExt(const std::vector<VkExtensionProperties> &v, const char *name) {
    for (const auto &e : v) if (std::strcmp(e.extensionName, name) == 0) return true;
    return false;
}

// Maps PCI vendor ID -> human-readable name. Same scheme as Vulkan-tools:
// https://pcisig.com/membership/member-companies (and the de-facto Vulkan IDs).
const char *vendorName(uint32_t id) {
    switch (id) {
        case 0x1002: return "AMD";
        case 0x10DE: return "NVIDIA";
        case 0x8086: return "Intel";
        case 0x13B5: return "ARM (Mali)";
        case 0x5143: return "Qualcomm (Adreno)";
        case 0x1010: return "Imagination (PowerVR)";
        case 0x14E4: return "Broadcom";
        case 0x106B: return "Apple";
        case 0x144D: return "Samsung Xclipse";
        case 0x1414: return "Microsoft (WARP)";
        default: return "unknown";
    }
}

// Map VkPhysicalDeviceType to a string.
const char *deviceTypeName(VkPhysicalDeviceType t) {
    switch (t) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "INTEGRATED_GPU";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "DISCRETE_GPU";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "VIRTUAL_GPU";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "CPU";
        default:                                     return "OTHER";
    }
}

// Append a single (label, OK?) row in a fixed-width style so the report stays
// readable when shared as plain text.
void appendCheck(std::string &out, const char *label, bool ok) {
    char line[256];
    std::snprintf(line, sizeof(line), "  [%s] %s\n", ok ? "Y" : "N", label);
    out += line;
}

// Format raw bytes (e.g., a heap size) as MiB.
std::string formatMiB(VkDeviceSize bytes) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.0f MiB", (double)bytes / (1024.0 * 1024.0));
    return std::string(buf);
}

// The whole probe runs inside this function. Returning a single std::string
// keeps the JNI side trivial (one GetStringUTFChars on the Kotlin side).
std::string runProbe() {
    std::string out;
    out.reserve(8192);
    out += "=== Vulkan probe ===\n";

    // ---- Instance ----
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "lsfgdiag";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "lsfgdiag";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    // Ask for 1.2: the LSFG session also asks for 1.2. If the loader caps us
    // back to 1.1 the instance still creates; we read the actual cap below.
    appInfo.apiVersion = VK_API_VERSION_1_2;

    // Check instance extensions before requesting them.
    uint32_t instExtCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &instExtCount, nullptr);
    std::vector<VkExtensionProperties> instExts(instExtCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &instExtCount, instExts.data());

    std::vector<const char *> requestedInstExts;
    for (const char *e : kInstanceExt) {
        if (hasExt(instExts, e)) requestedInstExts.push_back(e);
    }

    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &appInfo;
    ici.enabledExtensionCount = (uint32_t)requestedInstExts.size();
    ici.ppEnabledExtensionNames = requestedInstExts.data();

    VkInstance instance = VK_NULL_HANDLE;
    VkResult r = vkCreateInstance(&ici, nullptr, &instance);
    if (r != VK_SUCCESS) {
        char tmp[128];
        std::snprintf(tmp, sizeof(tmp), "vkCreateInstance FAILED (VkResult=%d)\n", (int)r);
        out += tmp;
        out += "\nThe Vulkan loader on this device cannot create an instance.\n";
        out += "This usually means the device's Vulkan driver is broken or absent.\n";
        return out;
    }

    // Loader-reported instance version. We linked libvulkan directly so this
    // symbol is always present at link time; safe to call unconditionally.
    {
        uint32_t loaderVersion = 0;
        if (vkEnumerateInstanceVersion(&loaderVersion) == VK_SUCCESS) {
            char tmp[128];
            std::snprintf(tmp, sizeof(tmp),
                          "Loader instance API: %u.%u.%u\n",
                          VK_VERSION_MAJOR(loaderVersion),
                          VK_VERSION_MINOR(loaderVersion),
                          VK_VERSION_PATCH(loaderVersion));
            out += tmp;
        }
    }
    out += "Instance extensions advertised:\n";
    appendCheck(out, VK_KHR_SURFACE_EXTENSION_NAME, hasExt(instExts, VK_KHR_SURFACE_EXTENSION_NAME));
    appendCheck(out, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME, hasExt(instExts, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME));

    // ---- Physical device(s) ----
    uint32_t pdCount = 0;
    vkEnumeratePhysicalDevices(instance, &pdCount, nullptr);
    if (pdCount == 0) {
        out += "\nNo Vulkan physical devices reported.\n";
        vkDestroyInstance(instance, nullptr);
        return out;
    }
    std::vector<VkPhysicalDevice> pds(pdCount);
    vkEnumeratePhysicalDevices(instance, &pdCount, pds.data());

    char tmp[256];
    std::snprintf(tmp, sizeof(tmp), "\n%u physical device(s)\n", pdCount);
    out += tmp;

    for (uint32_t i = 0; i < pdCount; ++i) {
        VkPhysicalDevice pd = pds[i];

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(pd, &props);

        std::snprintf(tmp, sizeof(tmp),
                      "\n--- Device %u ---\n"
                      "name:        %s\n"
                      "type:        %s\n"
                      "vendorID:    0x%04x  (%s)\n"
                      "deviceID:    0x%04x\n"
                      "driverVer:   0x%08x\n"
                      "API:         %u.%u.%u\n",
                      i,
                      props.deviceName,
                      deviceTypeName(props.deviceType),
                      props.vendorID, vendorName(props.vendorID),
                      props.deviceID,
                      props.driverVersion,
                      VK_VERSION_MAJOR(props.apiVersion),
                      VK_VERSION_MINOR(props.apiVersion),
                      VK_VERSION_PATCH(props.apiVersion));
        out += tmp;

        // ---- Device extensions ----
        uint32_t devExtCount = 0;
        vkEnumerateDeviceExtensionProperties(pd, nullptr, &devExtCount, nullptr);
        std::vector<VkExtensionProperties> devExts(devExtCount);
        vkEnumerateDeviceExtensionProperties(pd, nullptr, &devExtCount, devExts.data());

        std::snprintf(tmp, sizeof(tmp), "Device extensions: %u total\n", devExtCount);
        out += tmp;

        out += "Required by LSFG-Android session:\n";
        bool allRequiredOk = true;
        for (const char *ext : kRequiredDeviceExt) {
            const bool ok = hasExt(devExts, ext);
            if (!ok) allRequiredOk = false;
            appendCheck(out, ext, ok);
        }
        out += "Optional (gate fast paths):\n";
        for (const char *ext : kOptionalDeviceExt) {
            appendCheck(out, ext, hasExt(devExts, ext));
        }

        // ---- Features (the ones LSFG actually queries) ----
        // Build a chained feature struct so we get all three groups in one call.
        VkPhysicalDeviceShaderFloat16Int8FeaturesKHR fp16{};
        fp16.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR;

        VkPhysicalDeviceVulkanMemoryModelFeaturesKHR memModel{};
        memModel.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES_KHR;
        memModel.pNext = &fp16;

        VkPhysicalDeviceTimelineSemaphoreFeaturesKHR timeline{};
        timeline.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR;
        timeline.pNext = &memModel;

        VkPhysicalDeviceFeatures2 feats2{};
        feats2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        feats2.pNext = &timeline;

        // GetFeatures2 was promoted to core in 1.1, which is below our minSdk
        // floor (33 = Android 13 = Vulkan 1.1+ guaranteed), so this is safe.
        vkGetPhysicalDeviceFeatures2(pd, &feats2);

        out += "Critical LSFG features:\n";
        appendCheck(out, "shaderFloat16",         fp16.shaderFloat16 == VK_TRUE);
        appendCheck(out, "shaderInt8",            fp16.shaderInt8 == VK_TRUE);
        appendCheck(out, "vulkanMemoryModel",     memModel.vulkanMemoryModel == VK_TRUE);
        appendCheck(out, "timelineSemaphore",     timeline.timelineSemaphore == VK_TRUE);
        appendCheck(out, "samplerAnisotropy",     feats2.features.samplerAnisotropy == VK_TRUE);
        appendCheck(out, "robustBufferAccess",    feats2.features.robustBufferAccess == VK_TRUE);

        // ---- Memory heaps ----
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(pd, &memProps);
        std::snprintf(tmp, sizeof(tmp),
                      "Memory: %u type(s), %u heap(s)\n",
                      memProps.memoryTypeCount, memProps.memoryHeapCount);
        out += tmp;
        for (uint32_t h = 0; h < memProps.memoryHeapCount; ++h) {
            const auto &heap = memProps.memoryHeaps[h];
            std::snprintf(tmp, sizeof(tmp),
                          "  heap %u: %s%s\n",
                          h,
                          formatMiB(heap.size).c_str(),
                          (heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? " (DEVICE_LOCAL)" : "");
            out += tmp;
        }

        // ---- Queue families ----
        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> qfp(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, qfp.data());
        std::snprintf(tmp, sizeof(tmp), "Queue families: %u\n", qCount);
        out += tmp;
        for (uint32_t qf = 0; qf < qCount; ++qf) {
            const VkQueueFlags f = qfp[qf].queueFlags;
            std::snprintf(tmp, sizeof(tmp),
                          "  qf %u: count=%u flags=%s%s%s%s\n",
                          qf,
                          qfp[qf].queueCount,
                          (f & VK_QUEUE_GRAPHICS_BIT) ? "GRAPHICS " : "",
                          (f & VK_QUEUE_COMPUTE_BIT)  ? "COMPUTE "  : "",
                          (f & VK_QUEUE_TRANSFER_BIT) ? "TRANSFER " : "",
                          (f & VK_QUEUE_SPARSE_BINDING_BIT) ? "SPARSE" : "");
            out += tmp;
        }

        // ---- Verdict for this device ----
        out += "\nLSFG verdict for this device: ";
        if (!allRequiredOk) {
            out += "INCOMPATIBLE — at least one required device extension missing.\n";
        } else if (memModel.vulkanMemoryModel != VK_TRUE) {
            out += "RISKY — vulkanMemoryModel missing; the bundled DXBC→SPIR-V "
                   "shaders use it, expect vkCreateShaderModule rejection on probe.\n";
        } else if (fp16.shaderFloat16 != VK_TRUE) {
            out += "PARTIAL — runs in FP32; the FP16 framegen toggle will be greyed out.\n";
        } else {
            out += "LIKELY OK — all critical features present.\n";
        }
    }

    vkDestroyInstance(instance, nullptr);
    return out;
}

} // namespace

extern "C"
JNIEXPORT jstring JNICALL
Java_com_redclient_lsfgdiag_data_VulkanProbe_runProbeNative(JNIEnv *env, jclass) {
    std::string s;
    try {
        s = runProbe();
    } catch (const std::exception &e) {
        s = std::string("Probe threw exception: ") + e.what() + "\n";
    } catch (...) {
        s = "Probe threw unknown exception\n";
    }
    return env->NewStringUTF(s.c_str());
}
