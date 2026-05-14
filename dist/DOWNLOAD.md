# Download APKs

GitHub's web UI doesn't show a download button for large binary files.
Use these direct links instead:

## For your Android phone (arm64 — all modern phones)

**Direct download link:**
```
https://github.com/RedClient231/LSFG-MODIFIED/raw/fix/mali-ahb-stride-bug/dist/lsfg-android-patched-arm64-v8a-debug.apk
```

Or on your phone, open this URL in Chrome and it will download directly.

## For emulator (x86_64)

```
https://github.com/RedClient231/LSFG-MODIFIED/raw/fix/mali-ahb-stride-bug/dist/lsfg-android-patched-x86_64-debug.apk
```

## What's in this build

All 4 patches applied on top of LSFG-Android 0.1.2:

1. **AHB staging stride fix** — tile/colour-plane glitch on Mali/PowerVR
2. **PowerVR support** — same workarounds as Mali now cover PowerVR
3. **Shizuku smoke-test retry** — 3× retry before reporting firmware block
4. **compositeAlpha OPAQUE→INHERIT** — game was hidden behind black overlay; now game is visible through overlay

## Install

Enable "Install from unknown sources" on your phone, then open the APK.
