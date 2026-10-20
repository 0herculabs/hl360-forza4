#!/usr/bin/env python3
"""Build, validate and stage the native Metal game backend; no game launch."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import struct
import zlib
import shlex

from sdk_patches import apply_sdk_patches
from mac_launcher import stage_launcher
from macos_files import fixture_entries, copy_fixture_file

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "out/macos"


def run(args):
    print("+ " + " ".join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=ROOT, check=True)


def merge_tree(source, destination):
    source, destination = Path(source), Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    for entry, is_directory in fixture_entries(source):
        output = destination / entry.relative_to(source)
        if is_directory:
            output.mkdir(parents=True, exist_ok=True)
        else:
            output.parent.mkdir(parents=True, exist_ok=True)
            copy_fixture_file(entry, output)


def stage_game(sdk, deps, jobs, profile_fixture=None, profile_xuid=None, cache_fixture=None, stage_dir=None):
    build = OUT / "game-build"
    if not (build / "CMakeCache.txt").is_file():
        raise ValueError("Build the baseline game once before staging Metal")
    baseline = OUT / "vulkan-baseline-before-metal"
    names = ("fm4", "fm4-launcher", "fm4tool", "fm4.toml", "game_files.sha256", "cover.jpg")
    if not baseline.exists():
        baseline.mkdir()
        for source in [*(build / name for name in names), *build.glob("*.dylib")]:
            if source.is_file():
                shutil.copy2(source, baseline / source.name)
        (baseline / "files.json").write_text(json.dumps({p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in baseline.iterdir() if p.is_file()}, indent=2) + "\n")
    run(["cmake", "--build", build, "--target", "fm4", "fm4-launcher", "fm4-launcher-tests",
         "--parallel", jobs])
    run([build / "fm4-launcher-tests"])
    destination = Path(stage_dir).resolve() if stage_dir else OUT / "game-build-metal"
    destination.mkdir(parents=True, exist_ok=True)
    for source in [*(build / name for name in names), *build.glob("*.dylib")]:
        if source.is_file():
            shutil.copy2(source, destination / source.name)
    # Keep the diagnostic Vulkan choice functional in the Finder package.
    if (build / "vulkan").is_dir():
        merge_tree(build / "vulkan", destination / "vulkan")
    # Only this isolated game directory gets the full Metal plugin and its
    # matching runtime/dependencies. The installed SDK and Vulkan plugin stay.
    for source in (sdk / "out/mac-arm64/librexgpu-xenos.dylib", sdk / "out/mac-arm64/librexruntime.dylib",
                   deps / "lib/libdxilconv.dylib", deps / "lib/libmetalirconverter.dylib"):
        shutil.copy2(source, destination / source.name)
    # The SDK build's absolute search paths must not override the copies beside
    # the staged executable (or mask a missing packaged dependency during checks).
    library = destination / "librexgpu-xenos.dylib"
    commands = subprocess.check_output(["otool", "-l", str(library)], text=True)
    import re
    search_paths = re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset", commands)
    for search_path in search_paths:
        run(["install_name_tool", "-delete_rpath", search_path, library])
    run(["install_name_tool", "-add_rpath", "@loader_path", library])
    run(["codesign", "--force", "--sign", "-", library])
    config = (destination / "fm4.toml").read_text().replace('gpu_backend = "vulkan"', 'gpu_backend = "metal"')
    config += ("\n# Preserve draws and guest GPU writes while compiling cold shaders.\n"
               "metal_async_shader_translation = false\nmetal_async_pipeline_creation = false\n"
               "metal_sync_gpu_writes_for_guest = true\nmetal_sync_gpu_writes_skip_after_frames = 0\n"
               "\n# Suppress excessive sun glare until real occlusion queries are available.\n"
               "query_occlusion_fake_sample_count = 0\n")
    if (ROOT / "game/disc1/default.xex").exists():
        config += "\n# Development package: use the existing extracted Disc 1.\n"
        config += "game_data_root = " + json.dumps(str(ROOT / "game/disc1")) + "\n"
    (destination / "fm4.toml").write_text(config)
    arguments = ["python3", str(ROOT / "tools/Run-Mac.py"), "--backend", "metal", "--build-dir", str(destination),
                 "--keep-run-payloads", "1"]
    if profile_fixture:
        if not profile_fixture.is_dir():
            raise ValueError(f"Profile fixture does not exist: {profile_fixture}")
        arguments += ["--profile-fixture", str(profile_fixture.resolve())]
    if profile_xuid:
        arguments += ["--profile-xuid", profile_xuid]
    if cache_fixture:
        if not cache_fixture.is_dir():
            raise ValueError(f"Cache fixture does not exist: {cache_fixture}")
        arguments += ["--cache-fixture", str(cache_fixture.resolve())]
    command = destination / "Launch-Metal-Diagnostic.command"
    command.write_text("#!/bin/zsh\ncd " + shlex.quote(str(ROOT)) + "\nexec " +
                       " ".join(shlex.quote(value) for value in arguments) + ' "$@"\n')
    command.chmod(0o755)
    profile_command = destination / "Launch-Metal-Profile.command"
    profile_command.write_text("#!/bin/zsh\ncd " + shlex.quote(str(ROOT)) + "\nexec " +
                              " ".join(shlex.quote(value) for value in arguments) +
                              ' --warm-cache --perf "$@"\n')
    profile_command.chmod(0o755)
    performance = destination / "Launch-Performance.command"
    performance.write_text('#!/bin/zsh\ncd -- "$(dirname -- "$0")"\n'
        'exec ./fm4 --gpu_backend=metal --log_async=true '
        '--metal_sync_gpu_writes_skip_after_frames=90 "$@"\n')
    performance.chmod(0o755)
    stage_launcher(destination)
    receipt = {"backend": "metal", "gameplay_validated": False,
               "created_utc": datetime.now(timezone.utc).isoformat(),
               "launch_arguments": arguments,
               "files": {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in sorted(destination.iterdir()) if p.is_file() and p.name != "metal-backend.json"}}
    (destination / "metal-backend.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(f"Native Metal game staged: {destination}\nLaunch when ready: {command}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--alloy-source", type=Path, default=ROOT.parent / "xenia-alloy")
    parser.add_argument("--sdk-source", type=Path, default=ROOT / "thirdparty/rexglue-sdk")
    parser.add_argument("--sdk-build", type=Path, default=OUT / "sdk-build")
    parser.add_argument("--shader-cache", type=Path, help="Read-only shareable .xsh cache to validate")
    parser.add_argument("--draw-cache", type=Path, help="Render controlled offscreen draws with captured FM4 shaders")
    parser.add_argument("--native-check", action="store_true",
                        help="Run headless GPU submission and watched guest-memory checks")
    parser.add_argument("--game-backend", action="store_true", help="Build and stage the selectable game Metal backend")
    parser.add_argument("--stage-dir", type=Path, help="Stage a separate candidate package, preserving the existing Metal package")
    parser.add_argument("--runtime-cache", type=Path, help="Check the full game backend using this read-only shader cache")
    parser.add_argument("--window-check", action="store_true", help="Run a standalone Metal presentation/resize window")
    parser.add_argument("--profile-fixture", type=Path, help="Profile fixture copied by the staged launch command into each isolated run")
    parser.add_argument("--profile-xuid", help="Imported profile XUID for the staged launch command")
    parser.add_argument("--cache-fixture", type=Path, help="Read-only cache fixture copied into each isolated game run")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        parser.error("Use a native Apple Silicon macOS terminal")
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    alloy = args.alloy_source.resolve()
    deps = OUT / "metal-deps"
    # Reuse known working dependency binaries without running Alloy's build,
    # which may patch its original source. All copies stay in this workspace.
    files = {
        "dxc/include/DxbcConverter.h": "third_party/DirectXShaderCompiler-mac/projects/dxilconv/include/DxbcConverter.h",
        "lib/libdxilconv.dylib": "build-macos-arm64/third_party/dxilconv-build/lib/libdxilconv.dylib",
        "lib/libLLVMDxcSupport.a": "build-macos-arm64/third_party/dxilconv-build/lib/libLLVMDxcSupport.a",
        "lib/libmetalirconverter.dylib": "third_party/metal-shader-converter/lib/libmetalirconverter.dylib",
    }
    trees = {"dxc/include": "third_party/DirectXShaderCompiler-mac/include",
             "msc/include": "third_party/metal-shader-converter/include",
             "metal-cpp/include": "third_party/metal-cpp"}
    for source in (*files.values(), *trees.values()):
        if not (alloy / source).exists():
            parser.error(f"Missing reference dependency: {alloy / source}")
    for target, source in trees.items():
        merge_tree(alloy / source, deps / target)
    if args.game_backend or args.runtime_cache or args.window_check:
        cross = alloy / "third_party/SPIRV-Cross"
        if not cross.is_dir():
            parser.error(f"Missing reference dependency: {cross}")
        (deps / "spirv-cross").mkdir(exist_ok=True)
        for source in cross.iterdir():
            if source.is_file() and (source.suffix in (".cpp", ".hpp", ".h") or source.name == "LICENSE"):
                shutil.copy2(source, deps / "spirv-cross" / source.name)
    for target, source in files.items():
        (deps / target).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(alloy / source, deps / target)
    # Retain upstream notices with the local, untracked dependency copies.
    for source in ("LICENSE", "third_party/DirectXShaderCompiler-mac/LICENSE.TXT",
                   "third_party/metal-shader-converter/README.md"):
        if (alloy / source).is_file():
            target = deps / "notices" / source.replace("/", "_")
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(alloy / source, target)
    provenance = {"reference": str(alloy), "reference_revision": subprocess.check_output(
        ["git", "-C", str(alloy), "rev-parse", "HEAD"], text=True).strip(),
        "files": {str(p.relative_to(deps)): hashlib.sha256(p.read_bytes()).hexdigest()
                  for p, is_directory in sorted(fixture_entries(deps))
                  if not is_directory and p.name != "provenance.json"}}
    (deps / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    sdk = args.sdk_source.resolve()
    apply_sdk_patches(sdk, sorted((ROOT / "patches/sdk").glob("*.patch")))
    # The existing SDK cache retains the baseline toolchain and Vulkan settings.
    if not (args.sdk_build / "CMakeCache.txt").is_file():
        parser.error("Build the baseline SDK with tools/Build-Mac.py --stage sdk first")
    full_backend = args.game_backend or args.runtime_cache or args.window_check
    run(["cmake", "-S", sdk, "-B", args.sdk_build,
         "-DREXGLUE_BUILD_METAL_SHADER_TOOLS=ON", f"-DREXGLUE_METAL_DEPS_ROOT={deps}",
         *(["-DREXGLUE_USE_METAL=ON"] if full_backend else [])])
    run(["cmake", "--build", args.sdk_build, "--target", "rex-metal-shader-check",
         "rex-metal-submission-check", "rex-metal-memory-check", "rex-metal-draw-check",
         "--parallel", args.jobs])
    if full_backend:
        run(["cmake", "--build", args.sdk_build, "--target", "rexgpu-xenos", "--parallel", args.jobs])
    if args.runtime_cache:
        run(["python3", ROOT / "tools/Check-Metal.py", "--plugin", sdk / "out/mac-arm64/librexgpu-xenos.dylib", "--shader-cache", args.runtime_cache.resolve()])
    if args.window_check:
        run(["python3", ROOT / "tools/Check-Metal.py", "--plugin", sdk / "out/mac-arm64/librexgpu-xenos.dylib", "--window"])
    if args.game_backend:
        stage_game(sdk, deps, args.jobs, args.profile_fixture, args.profile_xuid, args.cache_fixture, args.stage_dir)
    if args.native_check:
        for name in ("rex-metal-submission-check", "rex-metal-memory-check"):
            run([sdk / "out/mac-arm64" / name])
    if args.draw_cache:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
        output = OUT / "metal-guest-draw" / stamp
        output.parent.mkdir(parents=True, exist_ok=True)
        run([sdk / "out/mac-arm64/rex-metal-draw-check", args.draw_cache.resolve(), output])
        # Render numeric RGBA readback as a PNG preview using only the stdlib.
        for raw in sorted(output.glob("*.rgba")):
            name = raw.stem
            data = raw.read_bytes()
            if len(data) != 128 * 128 * 4:
                raise ValueError("Unexpected offscreen readback size")
            def chunk(kind, body):
                return (struct.pack(">I", len(body)) + kind + body +
                        struct.pack(">I", zlib.crc32(kind + body) & 0xffffffff))
            rows = b"".join(b"\0" + data[y * 512:(y + 1) * 512] for y in range(128))
            png = (b"\x89PNG\r\n\x1a\n" +
                   chunk(b"IHDR", struct.pack(">IIBBBBB", 128, 128, 8, 6, 0, 0, 0)) +
                   chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))
            (output / (name + ".png")).write_bytes(png)
        print(f"Offscreen draw results: {output}")
    if args.shader_cache:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
        output = OUT / "metal-shader-check" / stamp
        output.parent.mkdir(parents=True, exist_ok=True)
        run([sdk / "out/mac-arm64/rex-metal-shader-check", args.shader_cache.resolve(), output])
        print(f"Results: {output}")


if __name__ == "__main__":
    main()
