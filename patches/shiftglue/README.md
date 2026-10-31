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
cmake --preset win-amd64 -DCMAKE_INSTALL_PREFIX=<repo>/thirdparty/sdk-shiftglue -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_USE_VULKAN=ON
```

`REXGLUE_USE_VULKAN` is off by default on Windows: without it `rexgpu-fh1`
has only D3D12 and the game fails to start ("requested backend 'vulkan' is
not compiled into this plugin").

```bat
cmake --build --preset win-amd64-release
cmake --install out/build/win-amd64 --config Release
```

The SDK downloads NVIDIA's DLSS runtime and AMD's FidelityFX headers at
configure time, checked by SHA-256. The installed SDK does not stage them for
an external game, so copy them into the install, where CMake picks them up
and copies them beside fm4.exe:

- `upscalers/dlss/310.9.16/`: `out/build/win-amd64/nvidia-dlss-runtime/`
  `nvngx_dlss.dll` and `LICENSE.txt`.
- `upscalers/fidelityfx/1.1.4/`: `out/build/win-amd64/ffx-api-headers/runtime/`
  `amd_fidelityfx_vk.dll` and `thirdparty/fidelityfx-vk/LICENSE.txt`. `scripts\build.cmd` uses
`thirdparty\sdk-shiftglue` when it exists. Regenerate the guest code with
this SDK's `rexglue.exe codegen fm4_manifest.toml` after changing revision.

Licence: BSD-3-Clause (see the SDK's LICENSE and THIRD_PARTY_NOTICES.md).
