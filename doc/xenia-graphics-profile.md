# Xenia-based Vulkan graphics profiles (v0.3.13)

The local working Xenia profile is the reference for Vulkan, a default 1280x720
guest video mode, async_shader_compilation=true, disabled resolve/memexport
readbacks and bilinear output. Game CPU code remains statically recompiled;
this is not the Canary executable or a guarantee of identical rendering.

Only the render scale, anisotropic filtering and output scaler are exposed
under Custom. Guest video resolution is fixed to 1280x720 on load and save.
Window size only controls the host window. FSR sharpness reduction is 0.2;
postprocess AA is disabled as in the reference. 4x/8x/16x refer to anisotropic
texture filtering, not MSAA.

| Preset | Scale | Anisotropic filtering | Output |
| --- | --- | --- | --- |
| Low | 1x | 4x | FSR |
| Medium (default) | 2x | 8x | FSR |
| High | 2x | 16x | Bilinear |
| Custom | 1x-4x | Off, 1x-16x | Bilinear, CAS, FSR |

Frame-rate selection is independent (60/120). VSync is selectable and defaults
to false, matching the reference. Guest vblank delivery remains independent:
the SDK delivers two vblanks per target racing frame, while the title's present
wrapper separately limits host frames to 60 or 120. Mapping the selected FPS
directly to guest vblank Hz previously halved the racing frame rate.
The SDK also defaults host_present_fps_limit to 60. The Vulkan host and launcher
explicitly set it to the selected 60/120 mode. Test with the installed base and
user configs: the original development base file disabled this separate limit,
masking the presentation cap in earlier source-frame-only measurements.
Opt-in frame_stats now logs actual host presents as well as guest frame calls.
No JIT or unsafe clock settings are copied.

The Vulkan host now maps VSync to the presenter on startup. Enabling it disables
immediate, mailbox and relaxed FIFO modes, requiring strict FIFO; disabling it
restores automatic mode selection. The launcher saves the same mapping. Restart
the game after changing VSync. This also selects sync interval 1 when Vulkan's
optional DXGI presentation path is used. The selected 60/120 FPS limit remains
independent; VSync does not change the Windows monitor refresh rate.

The Vulkan launcher restores original race render scenarios instead of applying
the older DX12 SkipShadowMapUnlessCockpit workaround. The native executor's
color overwrite, scrambled stencil and surfaceless clear shortcuts are disabled
to prefer preserving/transferring contents. This is a conservative diagnostic
change for green artifacts beneath the car, not a verified rendering fix.
The runtime still differs from Canary and requires a race comparison.

Showroom camera timing: the reference FM4 project corrects a fixed 1/60-second
camera queue step in Fm4ShowroomCameraStep (its 1.0 image, 0x824B9D84).
Our USA Rev 1 equivalent is 0x824BDCE4 in sub_824BDCA0, immediately after
loading float 1/60 from 0x820014BC and before the camera vtable +0xC call.
src/showroom_camera.cpp overrides only this function, retaining its generated
body and replacing that step with monotonic elapsed seconds per camera.
The original generated translation stays intact. New cameras start with one
console step; pauses are clamped to 100 ms. No racing physics is changed.
tests/showroom_timing_tests.cpp checks ten seconds of camera time at
30/60/120/240 updates per second, camera changes and pause handling.

The SDK port in patches/sdk/0006-vulkan-depth-bias-control.patch brings over
Canary's VK_EXT_depth_bias_control handling: exact constant UNORM bias units
where supported, float24 constant-factor adjustment, and a deferred command
for vkCmdSetDepthBias2EXT. Unsupported GPUs retain the existing bias path.
Source: https://github.com/xenia-canary/xenia-canary/tree/canary_experimental
The existing ShiftGlue module-reload and SDL haptic fixes are retained.

Validation: profile/custom save/reload tests, fixed-resolution migration,
frame-rate independence, audio persistence and physics restoration tests pass.
The Vulkan startup log on RTX 4070 SUPER confirms depthBiasControl,
leastRepresentableValueForceUnormRepresentation and depthBiasExact enabled,
async shaders=true, resolve=none and memexport=false. Full race A/B graphics
and stutter comparisons remain for gameplay testing. Shader warm-up automation
was cancelled by the user and is not included; playing builds the cache.

Build this Vulkan package from work/vulkan-trial/host into
work/vulkan-trial/build. The standard out/build/win-amd64-release tree is the
separate DX12 rollback build and must not supply this package's runtime DLLs.
