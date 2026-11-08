# Android runner

## Requirements

- Android Studio or Gradle 8.9 / JDK 17
- Android SDK 34, NDK 28.2.13676358 and CMake 3.22.1

## Android launcher

The app opens a Java launcher first. Use **SELECCIONAR ELF RECOMPILADO** to choose a local ELF through Android's Storage Access Framework. The launcher validates the ELF magic header and copies it to:

`Android/data/com.ps2x.runner/files/game.elf`

Then choose **INICIAR JUEGO** to open the native runtime. The native runtime is configured to load that exact path.

No broad storage permission is needed: the Android document picker grants access to the selected file.

## ISO limitation

PS2Recomp is a static recompilation runtime, not a PS2 emulator. This Android build does **not** load or convert PS2 ISO images. The ISO information button in the launcher explains this limitation; an ISO-to-recompiled-game pipeline has not been implemented.

## Build

Open the `android/` folder in Android Studio, or run:

```sh
cd android
gradle wrapper --gradle-version 8.9
./gradlew assembleRelease
```

APK output: `android/app/build/outputs/apk/release/app-release.apk`.

## Runtime notes

The Android runtime uses the configured default ELF path because NativeActivity does not provide command-line arguments. If the selected ELF is not a compatible PS2Recomp runtime input, the native runtime can still fail to load it.

Logs are written to logcat with tag `ps2x`.
