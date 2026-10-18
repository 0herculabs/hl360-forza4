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

## Stutter

Inspection of the SDK shows persistent per-title shader/pipeline storage and
blocking precompilation of cached pipelines at startup. New pipelines encountered
in gameplay still require compilation. The launcher exposes
no shader-compatibility controls. Synchronous compilation remains the default.
Developers can opt into `--async_shader_compilation=true` on the game command
line for experiments. Asynchronous
compilation may reduce those pauses by skipping draws while compilation is
pending; temporary missing objects/effects are a tradeoff. It does not remove
CPU, file-streaming or GPU-readback stalls. Do not delete caches to diagnose
stutter: that introduces cold-compilation pauses.

`frame_stats` is disabled by default and is not exposed in the launcher.
Developers can enable it with `scripts\run.cmd --frame_stats=true`.
Every five seconds `logs/fm4_*.log` reports mean, p99, worst frame interval and
missed frame budgets. Compare the same race twice, then compare async on/off.
There is no measured racing A/B result yet, so a stutter improvement is not
claimed. Keep 720p internal resolution as the initial comparison setting.

## Validation

CTest `launcher-compatibility` passes: reversible/idempotent edits, unchanged
UI XML, exact restoration, malformed input and external-edit handling, correct
verification mapping, and migration from old experimental shader/diagnostic settings to stable defaults. The Graphics
screen was rendered and visually inspected. The original game executable and
shared FM3 SDK binaries were not modified by this change.

The simplified Graphics screen was rebuilt and visually checked. The compatibility
regression suite passes, including migration of old experimental settings.

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
audio, replay and long sessions at 120 Hz still require validation. No simulation
clock multiplier or replacement physics files are applied. Revert to 60 FPS if
game speed or behavior changes. FPS also depend on CPU/GPU performance.
