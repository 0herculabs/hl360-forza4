#!/usr/bin/env python3
"""Remove obsolete generated payloads while preserving receipts and logs. Dry run by default."""
import argparse
import json
from pathlib import Path
import hashlib
from macos_files import fixture_entries, remove_fixture_tree

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "out/macos"

def completed_runs():
    finished = []
    for run in sorted((OUT / "test-runs").iterdir() if (OUT / "test-runs").is_dir() else (), reverse=True):
        receipt = run / "receipt.json"
        try:
            data = json.loads(receipt.read_text())
        except (OSError, ValueError):
            continue
        if data.get("process_state") == "exited" or data.get("cleanup_confirmed_inactive_pid"):
            finished.append(receipt.parent)
    return finished


def protected_fixtures():
    protected = set()
    for directory in (OUT.iterdir() if OUT.is_dir() else ()):
        if not directory.name.startswith('game-build-metal') or not directory.is_dir():
            continue
        try:
            arguments = json.loads((directory / 'metal-backend.json').read_text()).get('launch_arguments', [])
            for i, value in enumerate(arguments[:-1]):
                if value in ('--cache-fixture', '--profile-fixture'):
                    protected.add(Path(arguments[i + 1]).resolve())
        except (OSError, ValueError, TypeError):
            continue
    return protected


def candidates(keep, stale_builds=False, compact_profiles=False):
    finished = completed_runs()
    protected = protected_fixtures()
    for run in finished[keep:]:
        for name in ("FM4 Mac Test.app", "user", "cache"):
            # User data may contain new progress. Only compact_profile may
            # remove byte-identical files; never discard the whole directory.
            if name == "user":
                continue
            # The original baseline shader fixture is referenced by launch scripts.
            if name == "cache" and run.name == "20261016T115010Z-c787e3a0":
                continue
            path = run / name
            if path.resolve() in protected:
                continue
            if path.is_dir() and not path.is_symlink():
                yield path
    if stale_builds:
        for name in ("smoke-build", "tools-build", "game-build", "sdk-build"):
            build = OUT / name
            cache = build / "CMakeCache.txt"
            if cache.is_file() and not build.is_symlink():
                home = next((line.partition("=")[2] for line in cache.read_text().splitlines()
                             if line.startswith("CMAKE_HOME_DIRECTORY:INTERNAL=")), "")
                expected = ROOT / ("tools/macos-test" if name == "smoke-build" else
                                   "thirdparty/rexglue-sdk" if name == "sdk-build" else ".")
                if home and Path(home) != expected:
                    yield build


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(4 * 1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def compact_profile(run, baseline, hashes, apply):
    user = run / 'user'
    if not user.is_dir() or user.is_symlink():
        return 0
    if user.resolve() in protected_fixtures():
        print(f"Keeping launcher fixture: {user}", flush=True)
        return 0
    removed, retained, total = {}, [], 0
    for path, is_directory in fixture_entries(user):
        if is_directory:
            continue
        relative = str(path.relative_to(user))
        original = baseline / relative
        if original.is_file() and original.stat().st_size == path.stat().st_size:
            if relative not in hashes:
                hashes[relative] = digest(original)
            checksum = digest(path)
            if checksum == hashes[relative]:
                removed[relative] = checksum
                total += path.stat().st_size
                continue
        retained.append(relative)
    report = {'baseline': str(baseline), 'duplicate_files': removed,
              'retained_changed_or_unique_files': retained, 'duplicate_bytes': total}
    if apply:
        previous_path = run / 'profile-compaction.json'
        if previous_path.is_file():
            previous = json.loads(previous_path.read_text())
            if previous.get('baseline') != str(baseline):
                raise ValueError('Refusing to replace a compaction manifest with a different baseline')
            report['duplicate_files'] = previous.get('duplicate_files', {}) | removed
            report['duplicate_bytes'] += previous.get('duplicate_bytes', 0)
        # Save the reconstruction manifest before removing any byte-identical files.
        (run / 'profile-compaction.json').write_text(json.dumps(report, indent=2) + '\n')
        for relative in removed:
            (user / relative).unlink()
    print(f"{'Compacted' if apply else 'Would compact'} {run.name}: "
          f"{total / 1e9:.2f} GB duplicates; {len(retained)} changed/unique files retained", flush=True)
    return total

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--keep-runs", type=int, default=2, help="Number of completed run payloads to keep, plus all unfinished runs")
    parser.add_argument("--stale-builds", action="store_true", help="Also remove CMake build trees referring to a different source location")
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--compact-profiles", action="store_true",
                        help="Retain changed saves; remove only files SHA256-identical to the preserved xenia-user fixture")
    parser.add_argument("--remove-incomplete", action="store_true",
                        help="Remove payloads of preparations that never launched; preserve their logs/receipts")
    args = parser.parse_args()
    if args.keep_runs < 1:
        parser.error("Keep at least one completed run")
    baseline = OUT / 'xenia-user'
    if args.compact_profiles and not baseline.is_dir():
        parser.error('The preserved xenia-user fixture is required for safe profile compaction')
    if args.compact_profiles:
        hashes = {}
        for run in completed_runs()[args.keep_runs:]:
            compact_profile(run, baseline, hashes, args.apply)
    paths = list(candidates(args.keep_runs, args.stale_builds, args.compact_profiles))
    if args.remove_incomplete:
        for run in (OUT / 'test-runs').iterdir():
            if not run.is_dir() or run.is_symlink() or (run / 'process.log').exists():
                continue
            try:
                data = json.loads((run / 'receipt.json').read_text())
            except FileNotFoundError:
                data = {}
            except (OSError, ValueError):
                continue
            if data and data.get('process_state') != 'prepared':
                continue
            paths.extend(p for name in ('FM4 Mac Test.app', 'user', 'cache')
                         if (p := run / name).is_dir() and not p.is_symlink())
    for path in paths:
        print(("Removing: " if args.apply else "Would remove: ") + str(path), flush=True)
        if args.apply:
            remove_fixture_tree(path)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
