#!/usr/bin/env python3
"""Copy FM4 saves and install Xenia's Disc 2 packages into an isolated profile."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
from macos_files import fixture_entries, copy_fixture_tree

TITLE = '4D530910'
CONTENT_PACKAGES = 4


def sha256(path):
    h = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path.home() / '.local/share/Xenia/content')
    parser.add_argument('--profile-xuid', required=True)
    parser.add_argument('--destination', type=Path, default=ROOT / 'out/macos/xenia-user')
    parser.add_argument('--game-dir', type=Path, default=ROOT / 'game/disc1')
    parser.add_argument('--tool', type=Path, default=ROOT / 'out/macos/game-build/fm4tool')
    args = parser.parse_args()
    source, destination = args.source.resolve(), args.destination.resolve()
    if len(args.profile_xuid) != 16 or any(c not in '0123456789abcdefABCDEF' for c in args.profile_xuid):
        parser.error('--profile-xuid must be 16 hex digits')
    xuid = args.profile_xuid.upper()
    save = source / xuid / TITLE
    packages = source / '0000000000000000' / TITLE / '00000002'
    if destination.exists():
        parser.error('Destination already exists; choose a new directory to preserve previous imports')
    if not save.is_dir() or not packages.is_dir():
        parser.error('The selected FM4 save or common package directory is missing')
    if destination == source or source in destination.parents:
        parser.error('Destination must be outside the Xenia content directory')
    destination.mkdir(parents=True)
    receipt = {'created_utc': datetime.now(timezone.utc).isoformat(), 'source': str(source),
               'destination': str(destination), 'profile_xuid': xuid, 'title_id': TITLE, 'success': False}
    receipt_path = destination / 'import-receipt.json'
    try:
        source_files = sorted(p for p, is_directory in fixture_entries(save) if not is_directory)
        receipt['save_source_sha256'] = {str(p.relative_to(source)): sha256(p) for p in source_files}
        copy_fixture_tree(save, destination / xuid / TITLE)
        common = source / '0000000000000000' / TITLE
        for name in ('00000001', 'Headers'):
            if (common / name).is_dir():
                copy_fixture_tree(common / name, destination / '0000000000000000' / TITLE / name)
        for relative, digest in receipt['save_source_sha256'].items():
            if sha256(destination / relative) != digest or sha256(source / relative) != digest:
                raise ValueError(f'Save copy verification failed: {relative}')
        staging = destination / '.disc2-source'
        package_dir = staging / 'Content/0000000000000000' / TITLE / '00000002'
        package_dir.mkdir(parents=True)
        package_files = sorted(p for p in packages.iterdir() if p.is_file() and not p.name.startswith('.'))
        if len(package_files) != CONTENT_PACKAGES:
            raise ValueError(f'Expected {CONTENT_PACKAGES} Disc 2 packages, found {len(package_files)}')
        receipt['packages'] = {}
        for package in package_files:
            with package.open('rb') as stream:
                if stream.read(4) not in (b'LIVE', b'PIRS', b'CON '):
                    raise ValueError(f'Not an STFS content package: {package}')
            receipt['packages'][package.name] = {'bytes': package.stat().st_size, 'sha256': sha256(package)}
            # The SDK opens STFS inputs read-only. Avoid duplicating raw inputs
            # before extracting them into the destination's writable content tree.
            (package_dir / package.name).symlink_to(package)
        with (destination / 'install-dlc.log').open('w') as output:
            subprocess.run([str(args.tool.resolve()), 'install-dlc', str(args.game_dir.resolve()),
                            str(staging), str(destination)], check=True, stdout=output, stderr=subprocess.STDOUT)
        installed = destination / '0000000000000000' / TITLE
        for name in receipt['packages']:
            if not (installed / '00000002' / name).is_dir() or not (installed / 'Headers/00000002' / (name + '.header')).is_file():
                raise ValueError(f'Installed package or license header missing: {name}')
        for package in package_files:
            if sha256(package) != receipt['packages'][package.name]['sha256']:
                raise ValueError(f'Source package changed during import: {package}')
        for link in package_dir.iterdir():
            link.unlink()
        for directory in (package_dir, package_dir.parent, package_dir.parent.parent,
                          package_dir.parent.parent.parent, staging):
            directory.rmdir()
        receipt['success'] = True
        print(f'Imported FM4 save and {CONTENT_PACKAGES} DLC packages: {destination}')
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        receipt['error'] = str(error)
        print(f'Import failed: {error}; partial output preserved at {destination}', file=sys.stderr)
        return 1
    finally:
        receipt_path.write_text(json.dumps(receipt, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
