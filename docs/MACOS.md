# Experimental macOS support

This is an Apple Silicon adaptation of DarthMDev/hl360-forza3 commit
3865642710a69fc896767872944bde2aa85a00d8. It carries the native Metal SDK
renderer, MoltenVK graphics fixes, performance diagnostics and launcher support
into FM4. FM3 gameplay results are not FM4 validation results.

## Build

Use a native ARM64 terminal, Xcode command-line tools with C++23 support,
Python 3, CMake 3.25+ and Ninja. Build helpers explicitly target macOS 14.0
instead of inheriting the build host’s OS version. Create a dedicated SDK checkout; the builder
checks its revision and applies the ordered patch stack without resetting it.

```sh
git clone --recursive https://github.com/rexglue/rexglue-sdk.git thirdparty/rexglue-sdk
git -C thirdparty/rexglue-sdk checkout c94f5ebdcb3c9d1a460ca48e04f9758448f8d518
git -C thirdparty/rexglue-sdk submodule update --init --recursive
python3 tools/Build-Mac.py --stage preflight
python3 tools/Build-Mac.py --stage sdk
python3 tools/Build-Mac.py --stage smoke
python3 tools/Build-Mac.py --stage tools
```

The tools build needs no game assets. Use your own supported FM4 USA Rev 1
Disc 1 dump under `game/disc1/` (see [input identification](../doc/inputs.md)).

```sh
python3 tools/Build-Mac.py --stage game
python3 tools/Build-Metal.py --alloy-source /path/to/xenia-alloy --game-backend
```

Metal requires the locally built converter dependencies from Xenia Alloy, as
in the FM3 port. They are not downloaded by the build helpers. Release packages include the
runtime libraries and their notices.
Keep dependency licenses and notices. The Metal staging command copies matching
runtime/plugin libraries into `out/macos/game-build-metal` and creates
`FM4 Launcher.app`. Keep that bundle beside the game, tools, configuration and
libraries. Development packages require a writable folder for settings and saves;
they are locally signed, not notarized.

Disc image extraction is also available without the UI:

```sh
out/macos/tools-build/fm4tool iso /path/to/disc1.iso game/disc1
out/macos/tools-build/fm4tool iso /path/to/disc2.iso game/disc2
out/macos/tools-build/fm4tool install-dlc game/disc1 game/disc2 /path/to/user
```

Use a separate user directory for validation. `tools/Import-Xenia.py` can copy
an existing FM4 profile into a new directory and install its content packages;
the source profile remains separate from subsequent game runs.

On macOS the generated startup function registry is compiled at `-O0` with SelectionDAG and
without a precompiled header. This avoids excessive Apple Clang instruction-selection time for
its long sequence of registration calls; gameplay code retains Release flags.

## Settings

macOS defaults to the MoltenVK diagnostic baseline; the Metal package defaults
to Metal. The launcher offers both when a staged Metal backend is present.
Windows retains Direct3D 12. FM4 wheel controls and graphics workarounds remain
available. Controller vibration defaults off and can be enabled independently
of wheel force feedback.

The Frame rate row offers 30 FPS, 60 FPS / 60 Hz, and 120 FPS / 120 Hz.
60 FPS remains the default. The optional 30 FPS mode retains 60 Hz guest timing;
manual FM4 racing confirmed normal game speed without slowdown. 60 FPS restores the normal 60 Hz
mode without an extra host cap. 120 FPS selects the experimental 120 Hz guest
mode and clears the host cap. Restart the game after changing modes.

The launcher preserves the package’s existing `user` folder and discovers saved
FM4 accounts on first launch, including saves created by running `fm4` directly.
Keep this folder when updating the package. Metal staging enables the sun-glare
workaround described in [MACOS_METAL.md](MACOS_METAL.md).

Project Forza Plus installation still uses Windows PowerShell and is unavailable
on macOS. The normal Disc 1/Disc 2 extraction tools use SDL child processes and
native executable names.

See [Metal validation](MACOS_METAL.md) and [performance investigation](MACOS_PERFORMANCE.md) for test modes, the first-race analysis and launch choices.

## Release packaging

Use `tools/Package-Mac.py --build-dir out/macos/game-build-metal --version VERSION`
to create an allowlisted ZIP and SHA256 checksum. It excludes game dumps, DLC,
profiles, caches and personal settings, removes development paths from the base
config, checks every Mach-O deployment target and library dependency, and
re-signs the Finder launcher. Dependencies must also be rebuilt for the chosen
minimum macOS version; changing bundle metadata alone is insufficient.

The Metal renderer targets macOS 14 Sonoma or later on Apple Silicon. Apple’s
[shader converter requirements](https://developer.apple.com/metal/shader-converter/)
set macOS 14 as the baseline for its full generated-library feature set. An
explicit deployment target does not replace runtime testing on older macOS.

If the Alloy DXIL converter binary targets a newer OS, rebuild it in a separate
workspace directory before staging. For the pinned local Alloy sources:

```sh
cmake -S /path/to/xenia-alloy/third_party/DirectXShaderCompiler-mac \
  -B out/macos/dxilconv-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_CXX_STANDARD=17 \
  -DD3D12_INCLUDE_DIR=/path/to/xenia-alloy/third_party/DirectX-Headers/include/directx \
  -DDXGI_INCLUDE_DIR=/path/to/xenia-alloy/third_party/DirectX-Headers/include/directx \
  -DHLSL_BUILD_DXILCONV=ON -DHLSL_INCLUDE_TESTS=OFF \
  -DLLVM_TARGETS_TO_BUILD=None -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF \
  -DLLVM_ENABLE_BINDINGS=OFF -DLLVM_ENABLE_LIBXML2=OFF \
  -DLLVM_ENABLE_EH=ON -DLLVM_ENABLE_RTTI=ON
cmake --build out/macos/dxilconv-build --target dxilconv LLVMDxcSupport --parallel 4
python3 tools/Build-Metal.py --alloy-source /path/to/xenia-alloy \
  --dxil-build out/macos/dxilconv-build --game-backend
```
