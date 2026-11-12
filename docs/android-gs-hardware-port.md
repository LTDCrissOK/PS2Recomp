# Android GS hardware backend port (Manhunt 2)

## Status

This document records the first integration pass for the Android hardware GS port. It does **not** claim that a hardware renderer is implemented or selectable yet. The current Manhunt 2 runtime still constructs `GSCpuBackend`.

## Verified source differences

Reference: `GenissonEmilio/ShadowOfTheColossusPC`, `PS2Recomp/ps2xRuntime/src/lib/gs/gs_hw_backend.cpp` and `gs_hw_backend.h`.

Target: `LTDCrissOK/PS2Recomp`, branch `feature/android-video-output-manhunt2-0.1.1`.

1. The target's `GSRasterBackend` interface has the core GS calls (`Submit`, transfers, synchronization, presentation and VRAM access), but does not currently expose Shadow's optional `GSVuBatch` / `SupportsVuBatch` / `SubmitVuBatch` or `QueuePresent` extensions. The first Android port must use the common primitive path and must not import the VU1-specific path as if it existed.
2. The target frontend constructs `GSCpuBackend` directly in `GS::GS()` and its fallback initialization. There is no hardware-backend selection path yet.
3. Shadow's hardware source is about 3,000 lines and is not an Android backend as-is. Its GL include guards currently enable desktop Apple GL or VitaGL; Android GLES is not enabled.
4. The reference source includes target-side support files that are not present in this fork at the same paths: `gs_direct_frame.h`, `runtime/host_menu.h`, `runtime/ps2_stats.h`, and `runtime/ps2_thread_load.h`. These must be removed, replaced, or ported deliberately; adding the source file alone will not compile.
5. The reference GLSL uses `#version 330 core`; Android GLES 3 needs ES shader sources (GLSL ES 3.00 with precision declarations). GL context ownership is also different: Android must use the existing NativeActivity/raylib EGL context and must not create a second window/context.
6. The target's `GS::setRasterBackend()` already flushes/synchronizes the old backend, snapshots its 4 MiB local memory and initializes the replacement backend with the same VRAM allocation. This is the safe backend-switch seam to preserve.

## Port sequence

### A. Make the reference backend fit the target interface
- Start from the Shadow hardware backend's GS primitive/render-target implementation.
- Keep the target's existing `GSRasterBackend` ABI; initially omit the optional VU1 batch path.
- Retain a CPU-owned GS memory/transfer mirror for CLUT, uploads, local-to-host reads and unsupported operations. GPU render targets must be synchronized back before CPU reads or texture sampling of GPU-written memory.
- Keep the CPU backend available as an explicit fallback.

### B. Android GLES 3 backend adaptation
- Add an Android GL include path using GLES 3 headers and enable the hardware backend only for `ANDROID`.
- Provide GLES 3.00 shader variants; do not feed desktop GLSL 330 source to GLES.
- Audit every format/extension and framebuffer completeness result on real GLES 3 devices. In particular, depth/float render-target formats must be capability-checked and have a correct fallback.
- Use the already-current EGL context. No GLFW, no new EGL context, and no GL calls from a thread that does not own or share the context.
- Add context-loss/recreation handling and make backend initialization fail closed so CPU fallback remains usable.

### C. Runtime selection and validation
- Add an opt-in Android setting (default OFF) only after the backend compiles for both `armeabi-v7a` and `arm64-v8a`.
- Select hardware through the existing `GS::setRasterBackend()` hand-off, preserving VRAM contents and all CDVD/ELF/ISO/startup paths.
- Validate clear, untextured triangle, textured sprite, alpha test/blend, depth, CLUT, local-to-local transfer, local-to-host readback and presentation in that order.
- Do not call the renderer complete based on CI compilation alone. Require actual Manhunt 2 image output and logs from a physical Android device.

## Non-goals / protected paths

This work must not modify ISO selection, storage permissions, ELF detection, CDVD, game boot/startup, Android activity loading, or the LOG export feature. The app's existing video-output module is only a presentation/fallback path; it is not the GS hardware rasterizer.
