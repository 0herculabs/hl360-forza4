# Metal, graphics fixes and performance

The SDK patch series retains the FM3 port's source notices and provenance.
Apply all `patches/sdk/*.patch` in filename order to SDK revision
`c94f5ebdcb3c9d1a460ca48e04f9758448f8d518`; the FM4 module-unload fix is retained.
`Build-Mac.py --stage sdk` handles complete and partially applied prefixes.
Use an isolated SDK checkout rather than modifying another game's dependency.

Shared fixes include Vulkan texture exponent bias, macOS resize handling, native guest-module filename resolution,
Metal position invariance, resolve readback fences and POSIX directory typing.
The imported native Metal renderer adds shader conversion, render targets,
textures, depth/stencil/MSAA, pipeline caching/prewarming and GPU timing.
Optional asynchronous logging and stage diagnostics help investigate stutters.
These are source capabilities, not a promise of improved FM4 frame rates.

Base Metal staging waits for shader/pipeline creation and guest GPU writes.
Generated synchronization test commands opt into skipping repeated writes after
90 frames. This may affect correctness and remains opt-in. The Metal package
defaults `query_occlusion_fake_sample_count` to zero to suppress excessive sun
glare, matching the FM3 workaround. SDK patch 0028 permits zero in the cvar range.
This is a global fake-query result, not real occlusion support, and may hide other
query-dependent effects. Set it to `1000` to restore the SDK default. `readback_resolve` applies to Vulkan/D3D12; Metal
uses its own synchronization controls.

```sh
python3 tools/Run-Mac.py --backend metal --build-dir out/macos/game-build-metal --prepare-only
python3 tools/Run-Mac.py --backend metal --build-dir out/macos/game-build-metal --perf --warm-cache
python3 tools/Run-Mac.py --backend metal --build-dir out/macos/game-build-metal --cvar fm4_frame_limit=30
python3 tools/Check-Metal.py --window
```

Runs copy supplied profiles/caches and record hashes and logs. Warm caches are
selected only from successful runs with matching game/plugin hashes. Keep saves
from isolated runs explicitly before compacting them. Guest frame CSV/F3 measures
the FM4 present hook at `0x8237F598`, not completed display presentations.

Shader-cache/offscreen validation tools in the inherited SDK include captured
FM3-specific fixtures. Do not interpret those checks as FM4 shader validation.
FM4 must be tested separately from menus through racing, replays, saves and
Disc 2 installation. Windows regression builds and physical controller/wheel
vibration checks also remain separate acceptance work.

## Build and device validation

On Apple M3, ARM64 macOS 27.0.1 and Apple Clang 21:

- The SDK suite passed: 209 tests passed and four existing BitStream tests skipped.
- The tools-only build passed all 10 CTest checks (including 14 Python tooling tests).
- The game and both recompiled guest DLLs built; all 12 game CTest checks passed.
- Disc 1 passed the full file manifest; all four Disc 2 content packs installed.
- MoltenVK passed GPU fill, completion-fence and CPU readback checks.
- Native Metal passed 120 presentation frames, three resizes, shared-event and
  command-buffer submission checks, and watched guest-memory synchronization.

Manual FM4 testing through the launcher confirmed rendering and racing on Metal,
normal game speed in 30 FPS mode, and smoother racing after the logging changes.
The launcher profile-path mismatch was corrected to preserve direct-launch saves.
The sun-glare workaround was confirmed on an affected FM4 track.
Stable frame rates across all tracks and physical controller/wheel behavior
remain unverified.
