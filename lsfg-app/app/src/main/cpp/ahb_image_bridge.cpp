#include "ahb_image_bridge.hpp"
#include "android_shader_loader.hpp"  // for kOk

#include <volk.h>

#include <android/log.h>
#include <android/hardware_buffer.h>

#include <cstring>
#include <unistd.h>

#include "crash_reporter.hpp"

#define LOG_TAG "lsfg-vk-ahb"
#define LOGE(...) ::lsfg_android::ring_logf(LOG_TAG, ANDROID_LOG_ERROR, __VA_ARGS__)
#define LOGW(...) ::lsfg_android::ring_logf(LOG_TAG, ANDROID_LOG_WARN,  __VA_ARGS__)
#define LOGI(...) ::lsfg_android::ring_logf(LOG_TAG, ANDROID_LOG_INFO,  __VA_ARGS__)

namespace lsfg_android {

namespace {

uint32_t vk_to_ahb_format(VkFormat fmt) {
    switch (fmt) {
        case VK_FORMAT_R8G8B8A8_UNORM:        return AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
        case VK_FORMAT_R8G8B8_UNORM:          return AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM;
        case VK_FORMAT_R5G6B5_UNORM_PACK16:   return AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM;
        case VK_FORMAT_R16G16B16A16_SFLOAT:   return AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT;
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM;
        default: return 0;
    }
}

int wrap_ahb_in_vkimage(VulkanSession &vk, AhbImage &out) {
    AHardwareBuffer_Desc desc{};
    AHardwareBuffer_describe(out.ahb, &desc);

    VkAndroidHardwareBufferPropertiesANDROID ahbProps{
        .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,
    };
    VkAndroidHardwareBufferFormatPropertiesANDROID fmtProps{
        .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID,
    };
    ahbProps.pNext = &fmtProps;

    if (vk.fn.vkGetAndroidHardwareBufferPropertiesANDROID == nullptr) {
        LOGE("vkGetAndroidHardwareBufferPropertiesANDROID not loaded in session table");
        return kErrAhbProperties;
    }
    if (vk.fn.vkGetAndroidHardwareBufferPropertiesANDROID(vk.device, out.ahb, &ahbProps) != VK_SUCCESS) {
        LOGE("vkGetAndroidHardwareBufferPropertiesANDROID failed");
        return kErrAhbProperties;
    }

    // Pick the Vulkan format. The driver may report VK_FORMAT_UNDEFINED for
    // AHBs whose pixel layout isn't natively expressible (typically YCbCr
    // camera/video buffers). For those we'd need a VkSamplerYcbcrConversion
    // and the externalFormat path — that's not currently wired, so refuse
    // and let the caller decide what to do.
    VkFormat effectiveFormat = fmtProps.format;
    if (effectiveFormat == VK_FORMAT_UNDEFINED) {
        if (out.format != VK_FORMAT_UNDEFINED) {
            // Caller-allocated buffer: trust the format we asked AHardwareBuffer_allocate
            // for. Some drivers leave fmtProps.format UNDEFINED even for plain RGBA.
            effectiveFormat = out.format;
        } else {
            LOGE("AHB has no Vulkan format and no caller hint (externalFormat=0x%llx) — YCbCr path not implemented",
                 (unsigned long long)fmtProps.externalFormat);
            return kErrAhbImageCreate;
        }
    }

    VkExternalMemoryImageCreateInfo extImageInfo{
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
    };

    VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = &extImageInfo,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = effectiveFormat,
        .extent = { desc.width, desc.height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT
               | VK_IMAGE_USAGE_STORAGE_BIT
               | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
               | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vk.fn.vkCreateImage(vk.device, &imageInfo, nullptr, &out.image) != VK_SUCCESS) {
        LOGE("vkCreateImage failed for AHB %ux%u fmt=%d", desc.width, desc.height, (int)effectiveFormat);
        return kErrAhbImageCreate;
    }

    VkMemoryDedicatedAllocateInfo dedicated{
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .image = out.image,
    };
    VkImportAndroidHardwareBufferInfoANDROID importInfo{
        .sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
        .pNext = &dedicated,
        .buffer = out.ahb,
    };

    // Pick a memory type compatible with the AHB's allowed type bits.
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(vk.physicalDevice, &memProps);
    uint32_t typeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if (ahbProps.memoryTypeBits & (1u << i)) {
            typeIndex = i;
            break;
        }
    }
    if (typeIndex == UINT32_MAX) {
        LOGE("No memory type matches AHB memoryTypeBits=0x%x", ahbProps.memoryTypeBits);
        return kErrAhbMemoryAllocate;
    }

    const VkMemoryAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &importInfo,
        .allocationSize = ahbProps.allocationSize,
        .memoryTypeIndex = typeIndex,
    };
    if (vk.fn.vkAllocateMemory(vk.device, &allocInfo, nullptr, &out.memory) != VK_SUCCESS) {
        LOGE("vkAllocateMemory(AHB import) failed");
        return kErrAhbMemoryAllocate;
    }

    if (vk.fn.vkBindImageMemory(vk.device, out.image, out.memory, 0) != VK_SUCCESS) {
        LOGE("vkBindImageMemory failed");
        return kErrAhbBindMemory;
    }

    out.format = effectiveFormat;
    out.extent = { desc.width, desc.height };
    return kOk;
}

// Tries to export an OPAQUE_FD from AHB-imported memory. This is the fragile
// step from the plan: many drivers (especially Qualcomm Adreno) refuse the
// export because the memory's externalHandleType is ANDROID_HARDWARE_BUFFER,
// not OPAQUE_FD. We attempt anyway and return the FD or -1.
int try_export_opaque_fd(VulkanSession &vk, VkDeviceMemory mem) {
    if (vk.fn.vkGetMemoryFdKHR == nullptr) return -1;
    const VkMemoryGetFdInfoKHR info{
        .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
        .memory = mem,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR,
    };
    int fd = -1;
    const VkResult r = vk.fn.vkGetMemoryFdKHR(vk.device, &info, &fd);
    if (r != VK_SUCCESS) {
        return -1;
    }
    return fd;
}

} // namespace

int createAhbImage(VulkanSession &vk, uint32_t w, uint32_t h,
                   VkFormat fmt, AhbImage &out) {
    out = {};
    out.format = fmt;

    const uint32_t ahbFormat = vk_to_ahb_format(fmt);
    if (ahbFormat == 0) {
        LOGE("Unsupported VkFormat %d for AHB allocation", (int)fmt);
        return kErrAhbAllocate;
    }

    // CPU_READ_OFTEN (instead of RARELY) tells the driver we will lock these
    // AHBs frequently for CPU read — true on Adreno because blitOutputToWindow
    // locks every output AHB ~30-90 times/sec to memcpy into the overlay
    // ANativeWindow. With RARELY, each AHardwareBuffer_lock incurs a heavy
    // GPU→CPU cache flush; with OFTEN the driver picks a CPU-cached layout
    // and the lock cost drops by an order of magnitude. Tradeoff: GPU writes
    // through this memory may use a less optimal cache hierarchy, but for
    // our access pattern (one vkCmdCopyImage / vkCmdBlitImage per frame
    // followed by a CPU read) the savings outweigh the GPU-side cost.
    AHardwareBuffer_Desc desc{
        .width = w,
        .height = h,
        .layers = 1,
        .format = ahbFormat,
        .usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE
               | AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT
               | AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,
        .stride = 0,
        .rfu0 = 0,
        .rfu1 = 0,
    };
    if (AHardwareBuffer_allocate(&desc, &out.ahb) != 0 || out.ahb == nullptr) {
        LOGE("AHardwareBuffer_allocate(%ux%u, fmt=%u) failed", w, h, ahbFormat);
        return kErrAhbAllocate;
    }
    out.ownsAhb = true;

    const int rc = wrap_ahb_in_vkimage(vk, out);
    if (rc != kOk) {
        destroyAhbImage(vk, out);
        return rc;
    }

    // FD export is best-effort; on Adreno/Mali it always fails. The framegen
    // AHB path doesn't need it, so we don't warn anymore.
    out.exportedFd = try_export_opaque_fd(vk, out.memory);
    return kOk;
}

int importAhbImage(VulkanSession &vk, AHardwareBuffer *ahb, AhbImage &out) {
    out = {};
    if (ahb == nullptr) return kErrAhbAllocate;

    // ---- Mali staging path -------------------------------------------------
    // On ARM Mali drivers (MediaTek and some Samsung firmwares) the AHB that
    // MediaProjection's ImageReader hands us frequently arrives with a
    // vendor-specific tiled or compressed layout that the Adreno-targeted
    // import path mis-decodes — torn tiles, shifted colour planes, and
    // occasional vkAllocateMemory failures because the imported memory's
    // memoryTypeBits doesn't intersect any usable type.
    //
    // Mitigation: allocate a fresh, known-linear AHB ourselves (which uses
    // our explicit AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN + GPU_SAMPLED_IMAGE
    // flags from createAhbImage's path) and CPU-copy the source frame into
    // it via AHardwareBuffer_lock. Costs ~1-2 ms/frame at 1080p — measurable
    // but acceptable for a fix that converts "glitchy + crashy" into
    // "playable". On non-Mali devices we keep the zero-copy import path.
    if (vk.isMali) {
        AHardwareBuffer_Desc srcDesc{};
        AHardwareBuffer_describe(ahb, &srcDesc);

        // Default the destination format to the most common case
        // (MediaProjection ImageReader is RGBA_8888). Fall through to the
        // zero-copy path on exotic formats — better to take the chance on
        // those than to fail entirely.
        VkFormat dstVk = VK_FORMAT_UNDEFINED;
        switch (srcDesc.format) {
            case AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM:
            case AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM:
                dstVk = VK_FORMAT_R8G8B8A8_UNORM; break;
            default:
                LOGW("Mali AHB import: source format %u unsupported by staging path; "
                     "falling back to zero-copy import (may glitch)", srcDesc.format);
                break;
        }

        if (dstVk != VK_FORMAT_UNDEFINED) {
            // Allocate the staged copy via createAhbImage so it gets the
            // CPU_READ_OFTEN + GPU_SAMPLED_IMAGE + GPU_COLOR_OUTPUT usage
            // that the rest of the pipeline expects.
            AhbImage staged{};
            const int allocRc = createAhbImage(vk, srcDesc.width, srcDesc.height,
                                               dstVk, staged);
            if (allocRc != kOk) {
                LOGE("Mali AHB import: staging allocation failed rc=%d; "
                     "falling back to zero-copy", allocRc);
                destroyAhbImage(vk, staged);
                // fall through to zero-copy below
            } else {
                // Lock source for CPU read.
                void *srcBase = nullptr;
                int srcFence = -1;  // no GPU producer fence in our use
                int lockRcSrc = AHardwareBuffer_lock(ahb,
                    AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, srcFence, nullptr, &srcBase);
                if (lockRcSrc != 0 || srcBase == nullptr) {
                    LOGE("Mali AHB import: source AHardwareBuffer_lock failed rc=%d; "
                         "falling back to zero-copy", lockRcSrc);
                    destroyAhbImage(vk, staged);
                } else {
                    // Lock destination for CPU write.
                    void *dstBase = nullptr;
                    int lockRcDst = AHardwareBuffer_lock(staged.ahb,
                        AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, nullptr, &dstBase);
                    if (lockRcDst != 0 || dstBase == nullptr) {
                        LOGE("Mali AHB import: dest AHardwareBuffer_lock failed rc=%d; "
                             "falling back to zero-copy", lockRcDst);
                        AHardwareBuffer_unlock(ahb, nullptr);
                        destroyAhbImage(vk, staged);
                    } else {
                        // Copy row-by-row using the source's reported stride.
                        // Mali commonly reports a stride wider than width
                        // for 64-byte tile alignment; we still write a tight
                        // dest stride because our owned dest is plain RGBA.
                        const uint32_t bpp = 4u;  // RGBA_8888 / RGBX_8888
                        const uint32_t srcRowBytes = srcDesc.stride * bpp;
                        const uint32_t dstRowBytes = srcDesc.width * bpp;
                        const auto *srcBytes = static_cast<const uint8_t *>(srcBase);
                        auto *dstBytes = static_cast<uint8_t *>(dstBase);
                        for (uint32_t y = 0; y < srcDesc.height; ++y) {
                            std::memcpy(dstBytes + y * dstRowBytes,
                                        srcBytes + y * srcRowBytes,
                                        dstRowBytes);
                        }
                        AHardwareBuffer_unlock(staged.ahb, nullptr);
                        AHardwareBuffer_unlock(ahb, nullptr);
                        // Hand back the staged image. ownsAhb=true was set by
                        // createAhbImage so destroyAhbImage will free it
                        // correctly when the caller is done.
                        out = staged;
                        return kOk;
                    }
                }
            }
        }
    }
    // ---- end Mali staging path --------------------------------------------

    out.ahb = ahb;
    out.ownsAhb = false;

    // Hint: ImageReader is created with PixelFormat.RGBA_8888, so the imported
    // AHB is overwhelmingly likely to be R8G8B8A8_UNORM. Used as a fallback if
    // the driver reports VK_FORMAT_UNDEFINED in the properties query.
    AHardwareBuffer_Desc desc{};
    AHardwareBuffer_describe(ahb, &desc);
    switch (desc.format) {
        case AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM:
            out.format = VK_FORMAT_R8G8B8A8_UNORM; break;
        case AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM:
            out.format = VK_FORMAT_R8G8B8A8_UNORM; break;
        case AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM:
            out.format = VK_FORMAT_R8G8B8_UNORM; break;
        case AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM:
            out.format = VK_FORMAT_R5G6B5_UNORM_PACK16; break;
        case AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT:
            out.format = VK_FORMAT_R16G16B16A16_SFLOAT; break;
        case AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM:
            out.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32; break;
        default:
            out.format = VK_FORMAT_UNDEFINED; break;
    }

    const int rc = wrap_ahb_in_vkimage(vk, out);
    if (rc != kOk) {
        destroyAhbImage(vk, out);
        return rc;
    }
    // Don't even attempt FD export on imported buffers — they're not ours.
    out.exportedFd = -1;
    return kOk;
}

void destroyAhbImage(VulkanSession &vk, AhbImage &img) {
    if (img.exportedFd >= 0) {
        // FD was exported but never consumed by framegen. Close it ourselves.
        ::close(img.exportedFd);
        img.exportedFd = -1;
    }
    if (img.image != VK_NULL_HANDLE && vk.device != VK_NULL_HANDLE
            && vk.fn.vkDestroyImage != nullptr) {
        vk.fn.vkDestroyImage(vk.device, img.image, nullptr);
        img.image = VK_NULL_HANDLE;
    }
    if (img.memory != VK_NULL_HANDLE && vk.device != VK_NULL_HANDLE
            && vk.fn.vkFreeMemory != nullptr) {
        vk.fn.vkFreeMemory(vk.device, img.memory, nullptr);
        img.memory = VK_NULL_HANDLE;
    }
    if (img.ownsAhb && img.ahb != nullptr) {
        AHardwareBuffer_release(img.ahb);
    }
    img.ahb = nullptr;
    img.ownsAhb = false;
    img.extent = {0, 0};
    img.format = VK_FORMAT_UNDEFINED;
}

} // namespace lsfg_android
