# Bring-up notes

## Established

- Both USA Rev 1 discs extracted successfully with the FM3 SDK helper.
- Disc 1 verification manifest: 4,034 non-system-update files, approximately 7.99 GB.
- Four Disc 2 STFS packages installed through `ContentManager::InstallContent`,
  with a 328-byte licence/header file for each package.
- Main executable compiles to `fm4.exe`; XMediaFacade and SpeechFacade compile
  to separate native DLLs loaded through the generated module registry.
- Smoke runs initialize D3D12 on the local RTX 4070 SUPER, SDL input, an offline
  profile, the writable cache partition, the title shader store, and the GPU
  interrupt callback.
- XMediaFacade loads and registers 3,212 guest functions.

## Analysis fixes

- Main image: explicit tail-call targets, a getter with a shared epilogue,
  CRT setjmp/longjmp, native fibres, a ten-entry byte-indexed switch table,
  and indirect-call targets from data and materialized code pointers.
- FM4-specific protected-file resource rebuilding is available for the optional
  Project Forza Plus installation; see [integration details](forza-plus.md).
- The optional frame-statistics wrapper was relocated to FM4's present routine
  at `0x8237F598`, whose VdSwap call is at `0x8237F920`.
- XMediaFacade and SpeechFacade each need a virtual/adjustor tail-call target
  and indirect initializer thunks.
- EH continuation addresses must not be treated as independent functions:
  this produced truncated bodies and unresolved C++ labels during bring-up.

## Outstanding diagnostics

The code generator warns about a large generated function at `0x82317B70`
and two `bdz` targets at `0x827BF468` / `0x827BF46C` pointing at zero padding
(`0x827BF69C`). Do not interpret successful compilation as full correctness.
The multi-image tool also reports `BaseHeap::Release` cleanup diagnostics.

The SDK reports unimplemented filesystem/device kernel calls during startup,
and probes `update:\media.zip` when no title-update device is installed.
These messages preceded further startup progress and are not yet established
as the cause of a fatal failure.

The FM3 launcher has been adapted to FM4. Menus, movies, races, saves,
audio, frame timing, Kinect and content enumeration still need validation.
Runtime logs are local under `out/build/win-amd64-release/logs`.

## Latest smoke run

The final build completed successfully. With `--headless --frame_stats=true`,
the game reached its first present call, initialized the six-channel 48 kHz
audio endpoint, and kept presenting until a deliberate graceful window close.
Five-second presentation-rate samples were mostly 59.6–60.0 per second, with
57.5–59.0 samples during stalls. This measures calls from the guest present
routine, not verified gameplay frame rate or visual correctness.
The log records `Execution complete` after the requested close. No fatal
unregistered-function error occurred in that final run.

Launch locally with `scripts\run.cmd`; add `--frame_stats=true` for timing logs.
Visual inspection, menu input and an actual race remain unverified.

## FM4 launcher validation

- Native `fm4-launcher.exe` builds with vendored ImGui, stb_image and toml++;
  no mod ZIP dependency is needed.
- Profiles, graphics, controls, audio/language and installation screens were
  rendered with the launcher's screenshot mode and visually inspected.
- The launcher discovered the development installation and displayed four of
  four content packages and the FM4 artwork extracted from the user's disc.
- Full verification of the existing Disc 1 returned zero missing, size or hash errors.
- An isolated ISO import extracted 4,034 Disc 1 files and passed SHA-256 verification.
- An isolated Disc 2 ISO import installed all four packages successfully.
- Screenshots and test logs are local under `work/launcher-*`.
