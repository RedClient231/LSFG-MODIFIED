# LSFG-MODIFIED

Forked [FrankBarretta/LSFG-Android](https://github.com/FrankBarretta/LSFG-Android) **0.1.2** with targeted patches for Mali / MediaTek / PowerVR devices (Helio G99 + Mali-G57 MC2 was the test target; PowerVR support is grouped from code-reading because the symptom set is identical and the upstream code already calls it out by name).

## Layout

| Path | What it is |
|---|---|
| `lsfg-app/` | Forked LSFG-Android-Application with patches applied. The `app/` module here is what you build and install. |
| `lsfg-vk-android/` | The framegen submodule (volk, dxbc, pe-parse, toml11). Code unchanged from upstream — required by `lsfg-app`'s native CMake build. |
| `tools/lsfgdiag/` | Standalone diagnostic app that dumps Vulkan caps, captureDisplay reflection results, device info, and logcat into one shareable text file. Useful for triage on devices where the patches are insufficient. |
| `dist/` | Pre-built APKs from the most recent local build (`.gitignore`d, kept locally for fast download). |

## What the patches do

| # | File | Change |
|---|---|---|
| 1 | `lsfg-app/app/src/main/cpp/ahb_image_bridge.cpp` `importAhbImage()` | **Linear AHB staging on Mali / PowerVR.** When `vk.needsAhbStaging` (Mali OR PowerVR), allocates a fresh known-linear AHB and CPU-stages the source MediaProjection AHB through it via `AHardwareBuffer_lock` before wrapping in a `VkImage`. Costs 1–2 ms/frame; fixes tile/format glitches that show up as torn tiles, shifted colour planes, or `vkAllocateMemory` failures. Adreno path unchanged. The destination row stride is read back from `AHardwareBuffer_describe` after allocation so we don't write tight rows into a tile-aligned buffer (the original copy did, which silently re-introduced the same shift it was meant to fix). |
| 2 | `lsfg-app/app/src/main/cpp/android_vk_probe.{cpp,hpp}` + `lsfg_jni.cpp` + `NativeBridge.kt` + `ShaderExtractor.kt` | **Diagnostic strings on shader probe rejection.** The native probe records GPU name, vendor ID, `shaderFloat16`, `shaderInt8`, `vulkanMemoryModel` booleans, and rejected count. New `NativeBridge.getProbeRejectReason()` getter surfaces the reason to the UI; `ShaderExtractor.describe(-12)` now reads it instead of showing "Your GPU may not be compatible". |
| 3 | `lsfg-app/app/src/main/cpp/android_vk_session.cpp` | **Force CPU blit output on Mali / PowerVR.** When `vendorID == 0x13B5` (ARM) or `0x1010` (Imagination), sets `out.hasSwapchain = false` to bypass the WSI swapchain entirely. Removes presentation-time corruption when swapchain images are sourced from AHB-imported `VkImage`. The CPU-blit path is the documented fallback so this doesn't introduce new code paths. |
| 4 | `lsfg-app/app/src/main/java/com/lsfg/android/session/ShizukuCaptureUserService.kt` | **Detect vendor-patched `captureDisplay` at construct time.** Before entering the worker loop, tries up to three throwaway captures with 50 ms backoff (handles transient SurfaceFlinger warm-up failures observed on stock Adreno). If all three return `null`, surfaces a clear UI error pointing at the firmware patch (common on MediaTek HyperOS / OriginOS / Helio G99 firmware) and falls back to MediaProjection-only mode instead of silently spinning forever. |

Plus `android_vk_session.hpp` gains `vendorId`, `isMali`, `isPowerVR`, `needsAhbStaging`, and `disableSwapchain` flags so the patches are dispatchable without adding magic numbers in inner loops.

## Honest limitations

- These patches are **grounded in code-reading**, not in a logcat dump from your specific phone. Patch #1's CPU staging is the most defensible — it's a textbook fix for the symptom you described. Patches #2, #3, #4 are quality-of-life improvements that should also help on Mali but won't, by themselves, fix every glitch.
- If MediaProjection mode is still glitchy after these patches, install `lsfgdiag` (also in `dist/`) and share the report so we can apply patches grounded in your actual driver state.
- If your firmware has patched `SurfaceControl.captureDisplay` to return null, **no patch in this app can fix Shizuku mode** — the surface that the API exposes is gone. Patch #4 just makes that obvious instead of confusing.

## Build

### LSFG-Android-Application (the main app)
```bash
cd lsfg-app
./gradlew :app:assembleDebug
# APKs (ABI-split): app/build/outputs/apk/debug/app-{arm64-v8a,x86_64}-debug.apk
```
Requires JDK 17 + Android SDK 35 + NDK 27.0.12077973 (auto-installed by AGP).

### tools/lsfgdiag
```bash
cd tools/lsfgdiag
./gradlew :diag-app:assembleDebug
```

## Install

Direct APK links from the latest release:
- https://github.com/RedClient231/LSFG-MODIFIED/releases/latest

Workspace-local copies (fastest if you're using the sandbox):
- `dist/lsfg-android-mali-patches-arm64-v8a-debug.apk` (62 MB) — for Helio G99 and any other 64-bit ARM device
- `dist/lsfg-android-mali-patches-x86_64-debug.apk` (62 MB) — for x86_64 emulators
- `dist/lsfgdiag-v0.1.0-debug.apk` (54 MB) — diagnostic helper

## Stack (lsfg-app)

| | Version |
|---|---|
| Kotlin | 2.0.20 |
| AGP | 8.5.2 |
| Gradle | 8.9 |
| Compose BOM | upstream LSFG-Android |
| NDK | 27.0.12077973 |
| CMake | 3.22.1 |
| minSdk | 29 |
| compileSdk / targetSdk | 35 |

## Stack (tools/lsfgdiag)

| | Version |
|---|---|
| Kotlin | 2.1.0 |
| AGP | 8.7.3 |
| Gradle | 8.11.1 |
| NDK | 26.3.11579264 |
| minSdk | 33 |
