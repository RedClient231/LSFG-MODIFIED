#pragma once

// Persistent Vulkan instance/device for the LSFG session. Distinct from the
// transient context that android_vk_probe spins up for shader validation —
// this one stays alive for the duration of a capture session and owns the
// queue, command pool, and extension entry points needed for AHardwareBuffer
// interop and FD export.
//
// Note: framegen (LSFG_3_1::initialize) creates its OWN internal device
// matched by UUID. This session creates a parallel device on the same
// physical GPU so we can allocate AHB-backed images, export opaque FDs to
// pass into createContext, and blit outputs to the overlay Surface.

#include <volk.h>

#include <array>
#include <cstdint>
#include <string>

namespace lsfg_android {

constexpr int kSessionNoVulkan = -20;
constexpr int kSessionNoSuitableGpu = -21;
constexpr int kSessionMissingExtension = -22;
constexpr int kSessionDeviceCreateFailed = -23;
constexpr int kSessionAlreadyInitialized = -24;

// Size of the pre-allocated command-buffer / fence ring. The worker thread
// rarely has more than 2-3 submissions in flight (input copy, post-process,
// output blit * multiplier), so 8 gives comfortable headroom to rotate through
// without stalling on the oldest-slot fence.
constexpr uint32_t kCommandRingSize = 8;

struct VulkanSession {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    uint32_t computeFamilyIdx = UINT32_MAX;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    // Pre-allocated primary command buffers + paired signalling fences. Used
    // round-robin through acquireCommandRing() — waits on the slot's fence
    // (signalled by the previous use), resets CB, hands back {cb, fence}.
    // Replaces the per-frame vkAllocateCommandBuffers + vkQueueWaitIdle +
    // vkFreeCommandBuffers pattern which cost ~0.5-1 ms of driver overhead
    // per submission.
    std::array<VkCommandBuffer, kCommandRingSize> ringCommandBuffers{};
    std::array<VkFence, kCommandRingSize> ringFences{};
    std::array<bool, kCommandRingSize> ringFenceArmed{};
    uint32_t ringNext = 0;

    // Per-device function table. Required because framegen creates its own
    // VkDevice with a different extension set and calls volkLoadDevice() on it,
    // which clobbers volk's global function pointers. Anything in this session
    // that touches the Vulkan device MUST go through `fn.*` instead of the
    // globals.
    VolkDeviceTable fn{};

    // Per-instance function pointers we resolved against OUR instance. Same
    // problem as VolkDeviceTable above but at instance scope: framegen's
    // Instance::Instance() creates its own VkInstance without surface
    // extensions and calls volkLoadInstance() on it, which clobbers the global
    // vkCreateAndroidSurfaceKHR pointer (the new instance can't resolve it,
    // so volk overwrites the global with NULL). Save our resolved pointer
    // here at session-init time and use it from the render loop instead of
    // the volk global.
    PFN_vkCreateAndroidSurfaceKHR pfnCreateAndroidSurfaceKHR = nullptr;
    PFN_vkDestroySurfaceKHR pfnDestroySurfaceKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR pfnGetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR pfnGetPhysicalDeviceSurfaceFormatsKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR pfnGetPhysicalDeviceSurfaceSupportKHR = nullptr;


    // Device UUID packed the way framegen expects: vendorID<<32 | deviceID.
    // Pass this to LSFG_3_1::initialize.
    uint64_t deviceUuid = 0;

    // ---- Mali / MediaTek workaround flags (Phase B, Helio G99 patches) ----
    // Cached PCI vendor ID of the chosen physical device. 0x13B5 is ARM (Mali);
    // 0x5143 is Qualcomm (Adreno); 0x1010 is Imagination (PowerVR). The render
    // loop and AHB import path use these to enable hardware-specific
    // workarounds without affecting the supported Adreno path.
    uint32_t vendorId = 0;

    // True when vendorId == 0x13B5 (ARM). Used as a hot-path predicate so we
    // don't compare against the magic number in inner loops. Mali drivers on
    // MediaTek firmwares (e.g. Helio G99 + Mali-G57 MC2) ship in revisions
    // that expose AHardwareBuffer formats incompatible with how the Adreno
    // path imports them — see ahb_image_bridge.cpp::importAhbImage for the
    // staging-copy mitigation enabled by this flag.
    bool isMali = false;

    // True when the GPU is Imagination PowerVR (vendorId == 0x1010). The
    // service code in LsfgForegroundService and the upstream README both
    // call out the same symptoms on PowerVR as on Mali: AHB import quirks
    // and presentation-time corruption when the swapchain images come from
    // AHB-imported VkImages. We treat PowerVR like Mali for the staging /
    // CPU-blit workarounds rather than maintaining two parallel patch sets.
    bool isPowerVR = false;

    // True when the AHB import path needs the linear-staging mitigation
    // (currently: Mali OR PowerVR). Hot-path predicate consumed by
    // importAhbImage in ahb_image_bridge.cpp. Prefer this over checking
    // vendor IDs directly so future "also needs staging" devices can be
    // added in one place.
    bool needsAhbStaging = false;

    // True when the WSI swapchain output path is known-broken on this
    // driver and we must use the CPU-blit fallback instead — even when
    // the swapchain extension chain is otherwise present (currently:
    // Mali OR PowerVR). Read once at session-init and stored so render-loop
    // hot paths can skip the WSI attempt without re-deriving from vendor.
    bool disableSwapchain = false;

    // Whether the optional VK_EXT_robustness2 extension is enabled. Framegen
    // requires it; if false, framegen initialize() will fail and the caller
    // must surface a clear error to the user.
    bool hasRobustness2 = false;

    // Whether VK_EXT_queue_family_foreign is enabled. If true, AHB ownership
    // transitions use FOREIGN_EXT; otherwise EXTERNAL.
    bool hasQueueFamilyForeign = false;

    // Whether the instance + device carry the swapchain extension chain
    // (VK_KHR_surface, VK_KHR_android_surface, VK_KHR_swapchain). When true
    // the render loop can present generated frames via the WSI path instead
    // of CPU-blitting through ANativeWindow_lock. Swapchain setup happens
    // lazily in setOutputSurface() once we have the ANativeWindow.
    bool hasSwapchain = false;

    bool initialized() const { return device != VK_NULL_HANDLE; }
};

// Acquire a command buffer + fence from the ring. Blocks (via vkWaitForFences)
// until the slot's previous submission is retired, then resets both so the
// caller can record fresh commands. Returns true on success.
//
// Usage:
//   VkCommandBuffer cb; VkFence fence;
//   acquireCommandRing(vk, cb, fence);
//   record...
//   submitCommandRing(vk, cb, fence);
//   waitCommandRing(vk, fence); // or defer the wait past other CPU work
bool acquireCommandRing(VulkanSession &vk, VkCommandBuffer &outCb, VkFence &outFence);

// Submit `cb` on the compute queue, signalling `fence` when it retires.
// Does NOT wait. Use waitCommandRing(fence) when the caller actually needs
// the GPU to be done.
bool submitCommandRing(VulkanSession &vk, VkCommandBuffer cb, VkFence fence);

// Block until `fence` signals. Returns true if the wait succeeded (or the
// fence was never armed). Marks the fence as "pending reset" so the next
// acquireCommandRing() call on that slot resets it.
bool waitCommandRing(VulkanSession &vk, VkFence fence);

// Creates the persistent Vulkan instance + device with all extensions required
// for AHardwareBuffer interop, FD export, and framegen integration.
//
// Returns kOk on success; sets `out` fields. On failure returns one of the
// kSession* error codes and leaves `out` partially-populated — call
// destroy_session() to clean up.
int create_session(VulkanSession &out);

// Tears down everything in the session. Safe to call on a partially-initialized
// session (it will skip null handles).
void destroy_session(VulkanSession &s);

} // namespace lsfg_android
