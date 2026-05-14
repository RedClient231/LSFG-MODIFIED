# LSFG-MODIFIED

A workspace for diagnosing and (eventually) patching [LSFG-Android](https://github.com/FrankBarretta/LSFG-Android) compatibility issues on Mali / MediaTek devices.

## Phase A — `lsfgdiag/` (the diagnostic app, ready now)

`lsfgdiag` is a tiny standalone Android 13+ app that answers the four questions you actually need to know before patching LSFG-Android for a given device:

1. **What does this device's Vulkan driver actually expose?** — full instance/device cap dump that mirrors LSFG's `android_vk_session.cpp` queries (extensions, `shaderFloat16`, `vulkanMemoryModel`, queue families, memory heaps).
2. **Will the privileged screen-capture reflection chain even resolve?** — replicates `PrivilegedScreenCapture.kt`'s reflection step-by-step, stopping just before the call that needs shell UID. If `setUid(long)` is missing on this firmware, that single line of output explains why Shizuku mode "doesn't work."
3. **What does the device say about itself?** — manufacturer, SoC model, supported display modes, RAM, ABIs.
4. **What's recently in logcat?** — last 5,000 lines, with LSFG/Vulkan/SurfaceControl/Shizuku/native-crash tags pre-marked.

All four go into one shareable `.txt` file via the system Share sheet.

### Build / install

- Every push to `main` builds and publishes a debug APK to the rolling `latest-debug` release. Direct link:
  - https://github.com/RedClient231/LSFG-MODIFIED/releases/tag/latest-debug
- Workspace copies live under `dist/` for fast pull from this sandbox.
- Local build: `./gradlew :lsfgdiag:assembleDebug` (JDK 17, Android SDK 35, NDK 26.3.x).

### `READ_LOGS` note

On stock Android user builds, `logcat -d` only returns the calling app's own buffer entries. To make the logcat section actually contain LSFG-Android's logs, connect ADB and run:

```
adb shell pm grant com.redclient.lsfgdiag.debug android.permission.READ_LOGS
```

Then re-open the app and tap **Save & Share** again.

## Phase B — fork patches (waits on Phase A data)

Once we have a real diagnostic dump from your device, patches will land in this same repo under `lsfg-android-fork/`. Anything we ship there will be grounded in a specific log line, missing extension, or failing reflection step — not guesswork.

## Stack

| | Version |
|---|---|
| Kotlin | 2.1.0 |
| AGP | 8.7.3 |
| Gradle | 8.11.1 |
| Compose BOM | 2024.12.01 |
| NDK | 26.3.11579264 |
| CMake | 3.22.1 |
| minSdk | 33 (Android 13) |
| compileSdk / targetSdk | 35 (Android 15) |
