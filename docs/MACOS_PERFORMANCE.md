# macOS performance investigation

## First-race report

The first FM4 play session started with **11 cached Metal pipelines**. Prewarming
those took 9.5 ms, but a previously unseen race still needs new shader translation
and pipeline compilation. The staged configuration deliberately uses synchronous
shader/pipeline creation to preserve every draw. Cold compilation is therefore a
plausible explanation for early stutters, rather than an established measurement
of the race's bottleneck.

The same session recorded 47,352 visibility-query messages and 13,469 repeated
XamXStudioRequest stub warnings. Logging was synchronous. Patch 0027 moves the
per-query messages behind noisy debug logging and reports the XStudio stub once,
without changing its register behavior. macOS base configurations now enable the
existing asynchronous logger. Warnings and errors remain available.

The game used `fm4_frame_limit = 0`, 60 Hz guest timing and resolution scale 1.
The roughly 29 FPS screenshot does not establish that the host cap was enabled.
No live CPU sample was captured: the observed play process had already exited
when sampling was attempted. Race GPU timings were not enabled in that session.

## Launch choices

Quit the current game normally before starting another instance using its saves.

- `FM4 Launcher.app`: persistent settings, existing saves/cache, strict GPU waits.
- `Launch-Performance.command`: same persistent settings and saves/cache, async
  logging, and FM3's experimental `metal_sync_gpu_writes_skip_after_frames=90`.
  Repeated GPU-write destinations stop forcing waits after 90 consecutive frames;
  this trades synchronization guarantees for performance and needs FM4 visual testing.
- `Launch-Metal-Diagnostic.command` and `Launch-Metal-Profile.command` use
  `Run-Mac.py` to prepare isolated runs. Supply profile/cache fixtures when needed; they do not
  automatically use the ordinary launcher's profile.

Shader and pipeline creation remain synchronous in the performance command.
Their asynchronous alternatives skip draws while compilation is pending, so they
are not enabled merely to hide a first-race stall. Pipeline prewarming and disk
caches are already included; revisit the same race after a normal exit to compare
warm-cache behavior before attributing an improvement to changed settings.

For a persistent profiling run, from the staged package directory:

```sh
./fm4 --gpu_backend=metal --log_async=true --metal_perf_log=true \
  --frame_stats=true --frame_samples=frames.csv
```

Compare the same car, track, camera and race segment. Guest frame samples measure
present-hook calls, not completed display presentations or simulation speed.
Metal timing covers selected command buffers, not the whole GPU. Preserve logs
and distinguish cold-cache runs from warm ones. The isolated profiling and summary
helpers are `tools/Run-Mac.py --perf --warm-cache` and `tools/Summarize-Mac.py`.

## FM3 commit audit

All **20 SDK patches added by hl360-forza3 commit 3865642** are present byte-for-byte.
Native Metal, graphics fixes, pipeline caching/prewarming, synchronization presets,
GPU timing, asynchronous logging, the host cap and controller vibration were carried
across. The initial normal FM4 launch did not enable the experimental race preset.

The FM3-only thumbnail inspection utility is excluded because its XDC layout
assumptions have not been validated for FM4. Launcher regressions are covered by
FM4’s existing test suite. The Metal package also enables FM3’s zero-sample
sun-glare workaround.

Subsequent manual racing was reported smoother, and 30 FPS mode was confirmed
to retain normal game speed. This is gameplay feedback rather than a controlled
before/after benchmark; 60 FPS remains the default.
