#!/usr/bin/env python3
"""Run an isolated FM4 Vulkan or native Metal test; no time limit unless --seconds is provided."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import subprocess
import sys
import time
import uuid

from macos_files import copy_fixture_file, fixture_entries, isolate_thumbnail_copies
from mac_performance import ProcessSampler
from mac_cache import latest_metal_cache
from mac_launch_progress import LaunchProgress

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "out/macos"


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", choices=("vulkan", "metal"), default="vulkan")
    parser.add_argument("--build-dir", type=Path, default=OUT / "game-build")
    parser.add_argument("--sdk-install", type=Path, default=OUT / "sdk-install")
    parser.add_argument("--game-dir", type=Path, default=ROOT / "game/disc1")
    parser.add_argument("--profile-xuid", help="Sign in using this imported save profile (16 hex digits)")
    parser.add_argument("--profile-fixture", type=Path, help="Copy this user directory into the isolated test")
    parser.add_argument("--cache-fixture", type=Path, help="Copy this shader cache into the isolated test")
    parser.add_argument("--warm-cache", action="store_true",
                        help="Metal: copy the latest normally completed cache with matching plugin/game hashes; fall back to --cache-fixture")
    parser.add_argument("--prepare-only", action="store_true", help="Prepare and verify an isolated run without launching the game")
    parser.add_argument("--regenerate-thumbnails", action="store_true",
                        help="Diagnostic only: back up copied thumbnails; FM4 does not automatically regenerate missing files")
    parser.add_argument("--seconds", type=int, default=0)
    parser.add_argument("--keep-run-payloads", type=int, default=0,
                        help="After normal exit compact older test copies, keeping this many full runs (0 disables)")
    parser.add_argument("--perf", action="store_true", help="Record guest frame CSV, CPU stage diagnostics and process samples")
    parser.add_argument("--cvar", action="append", default=[], metavar="NAME=VALUE",
                        help="SDK diagnostic override, recorded in the run receipt (repeatable)")
    args = parser.parse_args()
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        parser.error("Use a native Apple Silicon macOS terminal")
    if not 0 <= args.seconds <= 3600:
        parser.error("--seconds must be 0 (no limit) or between 1 and 3600")
    if args.warm_cache and args.backend != "metal":
        parser.error("--warm-cache requires --backend metal")
    if args.keep_run_payloads < 0:
        parser.error("--keep-run-payloads must be nonnegative")
    build, install, game = args.build_dir.resolve(), args.sdk_install.resolve(), args.game_dir.resolve()
    binary = build / "fm4"
    driver = install / "share/vulkan/icd.d/MoltenVK_icd.json"
    run = OUT / "test-runs" / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8])
    try:
        for path in (binary, build / "fm4.toml", game / "default.xex", *((driver,) if args.backend == "vulkan" else ())):
            if not path.is_file():
                raise ValueError(f"Required input missing: {path}")
        driver_library = (driver.parent / json.loads(driver.read_text())["ICD"]["library_path"]).resolve() if args.backend == "vulkan" else None
        if driver_library is not None and not driver_library.is_file():
            raise ValueError(f"MoltenVK library missing: {driver_library}")
        cache_selection = "fixture"
        if args.warm_cache:
            cached = latest_metal_cache(OUT / "test-runs", sha256(build / "librexgpu-xenos.dylib"),
                                       sha256(game / "default.xex"))
            if cached:
                args.cache_fixture = cached
                cache_selection = "completed-compatible-metal-run"
            else:
                cache_selection = "fixture-fallback"
            print(f"Metal cache: {args.cache_fixture or 'cold'} ({cache_selection})", flush=True)
        run.mkdir(parents=True)
        print(f"Preparing {args.backend} run: {run}", flush=True)
        preparation_path = run / "preparation.json"
        preparation = {"backend": args.backend, "phase": "copying executable and libraries"}
        def report_preparation():
            temporary = preparation_path.with_suffix(".json.tmp")
            temporary.write_text(json.dumps(preparation, indent=2) + "\n")
            temporary.replace(preparation_path)
        report_preparation()
        print("Copying executable and renderer libraries…", flush=True)
        # Launch a private executable copy so ReXApp's sibling config and log
        # lookups cannot load or overwrite the user's normal launcher profile.
        bundle = run / "FM4 Mac Test.app"
        executable_dir = bundle / "Contents/MacOS"
        executable_dir.mkdir(parents=True)
        with (bundle / "Contents/Info.plist").open("wb") as plist:
            plistlib.dump({"CFBundleExecutable": "fm4", "CFBundleIdentifier": "org.hl360.fm4.test",
                           "CFBundleName": "FM4 Mac Test", "CFBundlePackageType": "APPL",
                           "NSHighResolutionCapable": True}, plist)
        copy_fixture_file(binary, executable_dir / "fm4")
        copy_fixture_file(build / "fm4.toml", executable_dir / "fm4.toml")
        libraries = sorted(build.glob("*.dylib"))
        for library in libraries:
            copy_fixture_file(library, executable_dir / library.name)
        fixtures = (("user", args.profile_fixture), ("cache", args.cache_fixture))
        fixture_identity = {}
        for name, fixture in fixtures:
            destination = run / name
            destination.mkdir()
            fixture_identity[name] = {}
            if fixture is None:
                continue
            fixture = fixture.resolve()
            if not fixture.is_dir():
                raise ValueError(f"Fixture directory missing: {fixture}")
            if name == 'user' and (fixture.parent / 'profile-compaction.json').is_file():
                raise ValueError('This run contains compacted save changes. Use the original fixture or reconstruct it from profile-compaction.json first.')
            entries = list(fixture_entries(fixture))
            files = sorted(p for p, is_directory in entries if not is_directory)
            total = sum(p.stat().st_size for p in files)
            completed = 0
            copied_files = 0
            started_copy = time.monotonic()
            last_report = started_copy
            print(f"Copying {name}: {len(files)} files, {total / 1e9:.2f} GB (independent copy)…", flush=True)
            preparation.update(phase=f"copying {name}", total_bytes=total, completed_bytes=0,
                               total_files=len(files), completed_files=0)
            report_preparation()
            def copy_progress(count):
                nonlocal completed, last_report
                completed += count
                now = time.monotonic()
                if now - last_report >= 2:
                    elapsed = max(now - started_copy, .001)
                    print(f"  {name}: {100 * completed / max(total, 1):.1f}% — "
                          f"{completed / 1e9:.2f}/{total / 1e9:.2f} GB, "
                          f"{completed / elapsed / 1e6:.1f} MB/s", flush=True)
                    preparation.update(completed_bytes=completed, completed_files=copied_files)
                    report_preparation()
                    last_report = now
            for source in files:
                relative = source.relative_to(fixture)
                target = destination / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                preparation["current_file"] = str(relative)
                digest = hashlib.sha256()
                copy_fixture_file(source, target, progress=copy_progress, hasher=digest)
                fixture_identity[name][str(relative)] = digest.hexdigest()
                copied_files += 1
            # Preserve empty directories too, without duplicating file reads.
            for directory, is_directory in entries:
                if is_directory:
                    (destination / directory.relative_to(fixture)).mkdir(parents=True, exist_ok=True)
            print(f"  {name}: complete; {copied_files} files copied and hashed in "
                  f"{time.monotonic() - started_copy:.1f}s", flush=True)
            preparation.update(completed_bytes=completed, completed_files=copied_files)
            report_preparation()
        thumbnail_backups = {}
        if args.regenerate_thumbnails:
            thumbnail_backups = isolate_thumbnail_copies(run / 'user', run / 'thumbnail-backup')
            print(f"Backed up {len(thumbnail_backups)} copied thumbnails for regeneration; source saves unchanged.", flush=True)
        print("Verifying launch files and writing receipt…", flush=True)
        preparation["phase"] = "verifying launch files"
        report_preparation()
        arguments = [str(executable_dir / "fm4"), f"--game_data_root={game}",
                     f"--user_data_root={run / 'user'}", f"--cache_root={run / 'cache'}",
                     f"--log_file={run / 'fm4.log'}", f"--gpu_backend={args.backend}", "--fullscreen=false",
                     "--window_width=1280", "--window_height=720", "--frame_stats=true", "--mnk_mode=true"]
        if args.profile_xuid:
            if len(args.profile_xuid) != 16 or any(c not in "0123456789abcdefABCDEF" for c in args.profile_xuid):
                raise ValueError("--profile-xuid must be 16 hex digits")
            arguments.append(f"--profile_xuid={args.profile_xuid.upper()}")
        if args.perf and args.backend == "metal":
            arguments.append("--metal_perf_log=true")
        for override in args.cvar:
            if not re.fullmatch(r"[a-z][a-z0-9_]*=[^\r\n]+", override):
                raise ValueError("--cvar must be NAME=VALUE without newlines")
            name = override.split("=", 1)[0]
            if name in {"game_data_root", "user_data_root", "cache_root", "log_file", "profile_xuid", "gpu_backend"}:
                raise ValueError(f"Use the dedicated argument for {name}")
            arguments.append(f"--{override}")
        env = os.environ.copy()
        if args.backend == "vulkan":
            env.update(REX_VULKAN_SDK=str(install), VK_DRIVER_FILES=str(driver), VK_ICD_FILENAMES=str(driver))
        # Override inherited diagnostics explicitly so a control run stays clean.
        env["FM3_PERF_LOG"] = "1" if args.perf else "0"
        if args.perf:
            arguments.append(f"--frame_samples={run / 'frames.csv'}")
        receipt = {"created_utc": datetime.now(timezone.utc).isoformat(), "arguments": arguments,
                   "binary_sha256": sha256(binary), "config_sha256": sha256(executable_dir / "fm4.toml"),
                   "xex_sha256": sha256(game / "default.xex"), "sdk_install": str(install),
                   "backend": args.backend,
                   "driver_sha256": sha256(driver) if driver_library else None, "driver_library": str(driver_library) if driver_library else None,
                   "driver_library_sha256": sha256(driver_library) if driver_library else None,
                   "libraries": {p.name: sha256(p) for p in libraries},
                   "seconds": args.seconds,
                   "performance_logging": args.perf,
                   "cache_selection": cache_selection,
                   "hardware": {"machine": platform.machine(), "macos": platform.mac_ver()[0],
                                "logical_cpus": os.cpu_count()},
                   "moltenvk_overrides": {k: v for k, v in env.items() if k.startswith("MVK_CONFIG_")},
                   "profile_fixture": str(args.profile_fixture.resolve()) if args.profile_fixture else "empty",
                   "profile_xuid": args.profile_xuid,
                   "cache_fixture": str(args.cache_fixture.resolve()) if args.cache_fixture else "cold",
                   "fixture_identity": fixture_identity,
                   "thumbnail_backups": thumbnail_backups,
                   "gameplay_validated": False, "running_at_timeout": False}
        receipt_path = run / "receipt.json"
        receipt["process_state"] = "prepared"
        receipt_path.write_text(json.dumps(receipt, indent=2) + "\n")
        preparation["phase"] = "ready"
        report_preparation()
        print(f"Isolated run ready: {run}", flush=True)
        if args.prepare_only:
            print("Preparation verified; --prepare-only did not launch FM4.", flush=True)
            return 0
        print("Starting FM4 now…", flush=True)
        print("Time limit: none" if args.seconds == 0 else f"Time limit: {args.seconds} seconds", flush=True)
        started = time.monotonic()
        with (run / "process.log").open("w") as output:
            process = subprocess.Popen(arguments, cwd=run, env=env, stdout=output, stderr=subprocess.STDOUT)
            receipt["pid"] = process.pid
            receipt["process_state"] = "running"
            receipt_path.write_text(json.dumps(receipt, indent=2) + "\n")
            sampler = ProcessSampler(process.pid, run / "process.csv") if args.perf else None
            launch_progress = LaunchProgress(run / "fm4.log") if args.backend == "metal" else None
            if launch_progress:
                launch_progress.start()
            if sampler:
                sampler.start()
            try:
                process.wait(timeout=args.seconds or None)
            except subprocess.TimeoutExpired:
                receipt["running_at_timeout"] = True
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            except BaseException:
                process.kill()
                process.wait()
                raise
            finally:
                if launch_progress:
                    launch_progress.finish()
                if sampler:
                    sampler.finish()
        receipt.update(exit_code=process.returncode, elapsed_seconds=time.monotonic() - started, process_state="exited")
        receipt_path.write_text(json.dumps(receipt, indent=2) + "\n")
        print(f"Exit: {process.returncode}; running at test limit: {receipt['running_at_timeout']}")
        print(f"Logs and receipt: {run}")
        if process.returncode == 0 and args.keep_run_payloads:
            print("Compacting older test copies; changed saves and logs are retained…", flush=True)
            cleanup = subprocess.run([sys.executable, str(ROOT / 'tools/Clean-Mac.py'),
                                      '--keep-runs', str(args.keep_run_payloads), '--compact-profiles', '--apply'])
            if cleanup.returncode:
                print("Cleanup did not finish; retained payloads can be cleaned later.", flush=True)
        return 0 if receipt["running_at_timeout"] else (0 if process.returncode == 0 else 1)
    except KeyboardInterrupt:
        if "preparation" in locals():
            preparation["phase"] = "interrupted"
            report_preparation()
        print("Run interrupted; partial copies and logs were retained.", file=sys.stderr)
        return 130
    except (OSError, ValueError, KeyError) as error:
        print(f"Mac run failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
