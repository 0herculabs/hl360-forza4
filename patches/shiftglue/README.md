# ShiftGlue SDK

The port renders with the Vulkan backend of i2kiselev's ShiftGlue SDK (a
ReXGlue fork), GPU plugin `rexgpu-fh1`. The pinned revision is the one
[fm4-static-recomp](https://github.com/i2kiselev/fm4-static-recomp) uses.

- Source: <https://github.com/i2kiselev/shiftglue-sdk>, branch
  `fm4-static-recomp`, commit `72b7dd6f`.
- `fm4-recomp-72b7dd6f.patch`: this project's changes on top of it, made
  from `patches/sdk` 0001 (profile setters), 0004 (content names),
  0005 (module unload path), 0007 (Vulkan depth bias control),
  0008 (restore the desktop before the hard exit), 0009 (presentation
  diagnostics) and the `video_mode_from_window` part of 0006. ShiftGlue
  already reuses import thunks (the rest of 0006 is D3D12 only).

## Building

```bat
git clone https://github.com/i2kiselev/shiftglue-sdk thirdparty\shiftglue-src
cd thirdparty\shiftglue-src
git checkout 72b7dd6f
git apply --ignore-whitespace ..\..\patches\shiftglue\fm4-recomp-72b7dd6f.patch
git -c core.longpaths=true submodule update --init --depth 1 -- <every submodule except moltenvk and libadrenotools>
```

On Windows, libmspack's symbolic links are checked out as one-line text
files; replace each with the file it names before building. Then, from a
`scripts\devenv.cmd` prompt:

```bat
cmake --preset win-amd64 -DCMAKE_INSTALL_PREFIX=<repo>/thirdparty/sdk-shiftglue -DREXGLUE_ENABLE_TRACY=OFF
cmake --build --preset win-amd64-release
cmake --install out/build/win-amd64 --config Release
```

The SDK downloads NVIDIA's DLSS runtime and AMD's FidelityFX headers at
configure time, checked by SHA-256. `scripts\build.cmd` uses
`thirdparty\sdk-shiftglue` when it exists. Regenerate the guest code with
this SDK's `rexglue.exe codegen fm4_manifest.toml` after changing revision.

Licence: BSD-3-Clause (see the SDK's LICENSE and THIRD_PARTY_NOTICES.md).
