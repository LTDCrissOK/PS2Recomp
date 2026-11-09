# PS2Recomp Vulkan Test (Android)

Standalone diagnostic APK. It does not load a game, ELF, ISO, Raylib, or PCSX2 and does not modify the PS2Recomp runner.

The native activity creates an Android Vulkan surface, selects a physical device with graphics/presentation support, creates a swapchain and render pass, then clears/presents each swapchain image in a changing color. A successful run proves Vulkan surface + swapchain + frame presentation on that device; it does not prove the PS2 GS renderer is implemented.

## Build

Open this folder as a separate Gradle project in Android Studio, or run:

```bash
gradle --project-dir android-vulkan-test assembleDebug
```

APK: `android-vulkan-test/app/build/outputs/apk/debug/app-debug.apk`

Supported ABIs: arm64-v8a and armeabi-v7a. Devices without Vulkan 1.0 / Android surface support will show an error in logcat and cannot pass this test.

Log tag: `PS2RecompVulkanTest`.
