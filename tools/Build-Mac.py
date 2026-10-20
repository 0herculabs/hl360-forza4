#!/usr/bin/env python3
"""Staged Apple Silicon builder. No game assets are needed for preflight/sdk/tools."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

from sdk_patches import apply_sdk_patches

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "out/macos"
PIN = "c94f5ebdcb3c9d1a460ca48e04f9758448f8d518"


def run(args, cwd=ROOT, capture=False, env=None, timeout=None):
    args = [str(arg) for arg in args]
    print("+ " + " ".join(args), flush=True)
    result = subprocess.run(args, cwd=cwd, check=True, text=True, capture_output=capture, env=env, timeout=timeout)
    return result.stdout.strip() if capture else None


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", choices=("preflight", "sdk", "smoke", "tools", "game"), default="tools")
    parser.add_argument("--sdk-source", type=Path,
                        default=Path(os.environ.get("REXSDK_DIR", ROOT / "thirdparty/rexglue-sdk")))
    parser.add_argument("--sdk-revision", default=PIN, help="Required SDK source commit")
    parser.add_argument("--sdk-install", type=Path, default=OUT / "sdk-install")
    parser.add_argument("--window-smoke", action="store_true", help="Exercise presentation and resize in the smoke stage")
    parser.add_argument("--build-dir", type=Path, help="Override the tools/game build directory")
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 1, 8))
    args = parser.parse_args()
    if args.window_smoke and args.stage != "smoke":
        parser.error("--window-smoke requires --stage smoke")
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        parser.error("Use a native Apple Silicon macOS terminal")
    OUT.mkdir(parents=True, exist_ok=True)
    receipt = {"stage": args.stage, "success": False,
               "created_utc": datetime.now(timezone.utc).isoformat(),
               "host": {"macos": platform.mac_ver()[0], "architecture": platform.machine()},
               "project_revision": run(["git", "rev-parse", "HEAD"], capture=True)}
    receipt_path = OUT / (args.stage + "-receipt.json")
    try:
        for tool in ("cmake", "ninja", "git", "xcrun"):
            if not shutil.which(tool):
                raise ValueError(f"Required tool not found: {tool}")
        cc = run(["xcrun", "--find", "clang"], capture=True)
        cxx = run(["xcrun", "--find", "clang++"], capture=True)
        sysroot = run(["xcrun", "--sdk", "macosx", "--show-sdk-path"], capture=True)
        receipt["compiler"] = run([cxx, "--version"], capture=True)
        probe = OUT / "preflight.cpp"
        probe.write_text('''#include <bit>
#include <expected>
#include <filesystem>
#if !defined(__aarch64__) || !defined(__APPLE__) || !defined(_LIBCPP_VERSION)
#error Native Apple Silicon and libc++ required
#endif
int main() {
  std::expected<int, int> v = 23;
  return v.value() == 23 && std::byteswap(0x12345678u) == 0x78563412u
      && !std::filesystem::current_path().empty() ? 0 : 1;
}
''')
        run([cxx, "-std=c++23", "-arch", "arm64", "-isysroot", sysroot,
             probe, "-o", OUT / "preflight"])
        run([OUT / "preflight"])
        common = ["-G", "Ninja", f"-DCMAKE_C_COMPILER={cc}", f"-DCMAKE_CXX_COMPILER={cxx}",
                  "-DCMAKE_OSX_ARCHITECTURES=arm64", f"-DCMAKE_OSX_SYSROOT={sysroot}",
                  "-DCMAKE_BUILD_TYPE=Release"]
        source, install = args.sdk_source.resolve(), args.sdk_install.resolve()
        if args.stage == "sdk":
            head = run(["git", "rev-parse", "HEAD"], cwd=source, capture=True)
            if head != args.sdk_revision:
                raise ValueError(f"SDK revision mismatch: expected {args.sdk_revision}, found {head}")
            submodules = run(["git", "submodule", "status", "--recursive"], cwd=source, capture=True)
            if any(line.startswith(("-", "+", "U")) for line in submodules.splitlines()):
                raise ValueError("Initialize SDK submodules at their pinned revisions before building")
            patches = sorted((ROOT / "patches/sdk").glob("*.patch"))
            receipt.update(sdk_source=str(source), sdk_revision=head, submodules=submodules,
                           patches={p.name: sha256(p) for p in patches})
            apply_sdk_patches(source, patches)
            diff = run(["git", "diff", "HEAD", "--binary", "--no-ext-diff"], cwd=source, capture=True)
            receipt["sdk_diff_sha256"] = hashlib.sha256(diff.encode()).hexdigest()
            build = OUT / "sdk-build"
            run(["cmake", "-S", source, "-B", build, *common,
                 f"-DCMAKE_INSTALL_PREFIX={install}", f"-DCPM_SOURCE_CACHE={OUT / 'dependency-cache'}",
                 "-DREXGLUE_USE_VULKAN=ON", "-DREXGLUE_USE_METAL=OFF",
                 "-DREXGLUE_BUILD_METAL_SHADER_TOOLS=OFF",
                 "-DREXGLUE_BUILD_TESTS=ON", "-DREXGLUE_BUILD_PPC_TESTS=OFF"])
            run(["cmake", "--build", build, "--parallel", args.jobs])
            run(["ctest", "--test-dir", build, "--output-on-failure", "--no-tests=error"])
            run(["cmake", "--install", build])
            receipt["cli_sha256"] = sha256(install / "bin/rexglue")
        elif args.stage == "smoke":
            build = OUT / "smoke-build"
            run(["cmake", "-S", ROOT / "tools/macos-test", "-B", build, *common,
                 f"-DSDL3_DIR={install / 'lib/cmake/SDL3'}",
                 f"-DCMAKE_PREFIX_PATH={install}", f"-Drexglue_DIR={install / 'lib/cmake/rexglue'}"])
            run(["cmake", "--build", build, "--parallel", args.jobs])
            driver = install / "share/vulkan/icd.d/MoltenVK_icd.json"
            env = os.environ.copy()
            env.update(REX_VULKAN_SDK=str(install), VK_DRIVER_FILES=str(driver), VK_ICD_FILENAMES=str(driver))
            test_binary = build / "fm4-macos-sdk-test.app/Contents/MacOS/fm4-macos-sdk-test"
            test_args = ["--test_window=true", "--test_resize=true", "--test_window_hold_seconds=60"] if args.window_smoke else []
            run([test_binary, *test_args], cwd=build, env=env, timeout=85)
            receipt.update(sdk_install=str(install), driver_sha256=sha256(driver),
                           window_smoke=args.window_smoke, test_binary_sha256=sha256(test_binary))
        elif args.stage in ("tools", "game"):
            cli = install / "bin/rexglue"
            if not cli.is_file():
                raise ValueError(f"SDK missing at {install}; run --stage sdk or pass --sdk-install")
            receipt.update(sdk_install=str(install), cli_sha256=sha256(cli))
            receipt["sdk_libraries"] = {str(p.relative_to(install)): sha256(p)
                                        for p in sorted((install / "lib").glob("*.dylib"))}
            provenance = OUT / "sdk-receipt.json"
            if provenance.exists():
                receipt["sdk_receipt_sha256"] = sha256(provenance)
            game = args.stage == "game"
            if game:
                if not (ROOT / "game/disc1/default.xex").is_file():
                    raise ValueError("Place your supported Disc 1 dump at game/disc1 before codegen")
                xex = ROOT / "game/disc1/default.xex"
                expected = next(line.split()[0] for line in
                                (ROOT / "launcher/data/game_files.sha256").read_text().splitlines()
                                if line.endswith(" default.xex"))
                digest = sha256(xex)
                if digest != expected:
                    raise ValueError("Disc 1 default.xex does not match the supported FM4 USA Rev 1 build")
                receipt["xex_sha256"] = digest
                run([cli, "codegen", ROOT / "fm4_manifest.toml"])
            build = args.build_dir.resolve() if args.build_dir else OUT / ("game-build" if game else "tools-build")
            run(["cmake", "-S", ROOT, "-B", build, *common, "-DREXSDK_DIR=",
                 f"-DSDL3_DIR={install / 'lib/cmake/SDL3'}",
                 f"-DCMAKE_PREFIX_PATH={install}", f"-Drexglue_DIR={install / 'lib/cmake/rexglue'}", f"-DFM4_BUILD_GAME={'ON' if game else 'OFF'}",
                 "-DFM4_BUILD_TESTS=ON"])
            run(["cmake", "--build", build, "--parallel", args.jobs])
            run(["ctest", "--test-dir", build, "--output-on-failure", "--no-tests=error"])
            receipt["binaries"] = {name: sha256(build / name) for name in
                                   (["fm4", "fm4tool", "fm4-launcher"] if game else ["fm4tool", "fm4-launcher"])}
            receipt["config_sha256"] = sha256(build / "fm4.toml")
        receipt["success"] = True
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        receipt["error"] = str(error)
        print(f"Build failed: {error}", file=sys.stderr)
        return 1
    finally:
        receipt_path.write_text(json.dumps(receipt, indent=2) + "\n")
        print(f"Receipt: {receipt_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
