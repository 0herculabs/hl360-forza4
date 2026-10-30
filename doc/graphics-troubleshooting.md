# Shadows and intermittent stutter

## Race shadow workaround

The launcher automatically applies a reversible race-only workaround based
on [xenia-canary/game-patches issue 398](https://github.com/xenia-canary/game-patches/issues/398).
It adds `SkipShadowMapUnlessCockpit=1` to supported race, pre-race, post-race
and replay XML scenarios. It skips exterior shadow maps rather than correcting
the GPU shadow renderer; cockpit shadow maps remain. UI scenarios and global
settings are deliberately excluded: the issue reports career/prize-car problems
when the setting is forced globally. The published EU executable patch targets
a different module hash from our USA Rev 1 build and is not applied.

Original bytes are backed up under `<game>/.fm4-shadow-originals`. The developer
CLI `fm4-launcher --race-shadows off --game-dir <dir>` restores those bytes;
normal installation/verification and launching reapply the workaround. Verification checks backed-up originals only when
the live file exactly matches the expected edit; unrelated edits remain visible
to the integrity checker. Do not remove the backup folder while enabled.

The workaround is currently enabled in the local installation. It still needs
confirmation on the user's affected race. Startup and the present loop were
smoke-tested successfully after activation. The full SHA-256 check passed with
zero missing files, size mismatches or hash mismatches.

## Performance profile adapted from local Xenia Canary settings

The local FM4 profile in `D:\Emulation\emuladores\x360\Emulators\Xenia Canary\config\Forza Motorsport 4 (Disc 1) (Play Disc).config.toml`
uses asynchronous shader compilation, no resolve readback, 3x internal resolution,
8x anisotropic filtering, no post-process AA and bilinear presentation. These
settings were applied to `out/build/win-amd64-release/fm4.user.toml`; its previous
contents are saved in `work/xenia-profile-backup-20261019/fm4.user.toml`.
The Xenia installation itself was not changed.

The port defaults now enable asynchronous shaders and disable resolve readback.
The launcher exposes both choices under Graphics and preserves them when saving.
Existing user choices remain in effect until changed. Resolution and filtering
remain user settings; Restore defaults keeps the original 720p baseline.
Async compilation can skip draws temporarily while a pipeline is being created.
If roads or car thumbnails disappear with readback disabled, select Fast in
Graphics. Full readback can be expensive. Restart the game after changes.
Persistent shader caches are retained.

Xenia's `vsync = false` is intentionally adapted to `vsync = true` here:
the installed SDK uses this switch for guest vblank pacing and selects a 1000 Hz
vblank timer when disabled. The normal target remains 60 Hz. CPU JIT options
cannot be transferred to statically recompiled code. Other GPU options already
matching SDK defaults were left alone.

Validation: the launcher builds and the launcher-compatibility test passes,
including persistence of async and readback choices. No race A/B benchmark has
been performed; an FPS improvement has not yet been measured. Compare the same
race and camera with a warm cache. `--frame_stats=true` enables timing reports
in game logs. Lowering internal resolution to 1x can help isolate GPU load.

## Experimental 120 FPS / 120 Hz

Graphics > Frame rate selects 60 FPS / 60 Hz (default) or the experimental
120 FPS / 120 Hz target. Restart the game to apply it. The launcher writes
video_mode_refresh_rate = 120.0 and keeps vsync enabled. The runtime uses that
guest video refresh for its vblank timer; it does not change the Windows display
mode. Configure a capable monitor to 120 Hz or higher in Windows separately.
Returning to 60 FPS writes 60.0; Restore defaults also selects 60 FPS.

A 20-second native startup smoke test reached 120.0 guest present calls per
second, averaging 8.33 ms (last five-second sample). This is startup validation,
not a race benchmark or proof of correct simulation timing. Physics, animations,
audio, replay and long sessions at 120 Hz still require validation. No simulation clock multiplier is applied. Forza Plus can use the supplied replacement physics profile described below. Revert to 60 FPS if
game speed or behavior changes. FPS also depend on CPU/GPU performance.

## Recommended profile supplied by the user

The subsequently supplied Xenia configuration uses 1x rendering (1280x720),
vsync enabled and both D3D12 resolve/memexport readbacks disabled. The active
local user configuration now uses 1x rather than the previous 3x, reducing
rendered pixel count by a factor of nine. The port base configuration explicitly
disables shared memexport readback too (the SDK shared default was enabled).
The previous user configuration is backed up in
`work/fm4-recommended-profile-backup-20261019/fm4.user.toml`.

The supplied file does not specify async shader compilation or anisotropic
filtering, so the adapted async choice and 8x filtering are retained. Its old
JIT settings and `vsync_interval` are not imported. Audio queue values are not
copied blindly: the current SDK enforces a minimum of four frames, so the old
`max_queued_frames = 3` cannot be reproduced directly. Racing performance still
requires measurement; the pixel reduction is not an FPS benchmark.

## Forza Plus 120 FPS physics

When launching an installation marked `.fm4-plus-ready`, the launcher applies
`config/forza-plus-120-PhysicsSettings.ini` (embedded in the launcher) if 120 FPS
is selected. It uses the entire user-supplied file, including TimeScale 1 and
CarPhysicsUpdateHz 695. This is a physics update rate, not a 695 FPS target.
At 60 FPS, Play restores the exact previous bytes. Original-game installations
are unaffected. Changes take effect on Play, before starting the game.

The prior file is retained as `.fm4-120-physics-original.ini` in the mod game
root. Verification checks that backup against the original mod manifest only
when the active file exactly matches the embedded profile. External edits are
reported and never overwritten by profile switching. Do not delete the backup.
The launcher build and compatibility tests pass, covering original-game
exclusion, repeated activation, backup verification, external modifications and
exact restoration. Physics behavior and performance in races remain untested.

## Performance controls hidden

The Performance card has been removed at the user's request. Existing shader and readback values are preserved when saving, and Restore defaults no longer modifies them. The working Desktop installation retains async_shader_compilation=false and readback_resolve=fast. Only the launcher executable was replaced; the running launcher must be reopened to display the change.


## Forza Plus visual preset

Graphics includes a Forza Plus visual preset button. It selects guest video mode 1920x1080, resolution_scale=1, present_effect=fsr and present_fsr_sharpness_reduction=0.15, adapting the supplied Xenia visual guide. Guest video mode is a request to the game and does not guarantee every render target is native 1080p. No frame pacing, shader/readback or physics settings are changed. These values persist across launcher saves. Restart the game after applying. Gameplay appearance and performance still require confirmation.

