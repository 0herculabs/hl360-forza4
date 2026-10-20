#!/usr/bin/env python3
"""Create a clean Apple Silicon release ZIP, checking deployment targets and dependencies."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import zipfile
from mac_launcher import stage_launcher
from macos_files import fixture_entries

ROOT = Path(__file__).resolve().parents[1]
FILES = ('fm4', 'fm4-launcher', 'fm4tool', 'fm4.toml', 'game_files.sha256',
         'librexruntime.dylib', 'librexgpu-xenos.dylib', 'libfm4_XMediaFacade_default.dylib',
         'libfm4_SpeechFacade_default.dylib', 'libdxilconv.dylib', 'libmetalirconverter.dylib')
PRIVATE_KEYS = {'game_data_root', 'user_data_root', 'profile_name', 'profile_xuid', 'frame_samples'}


def clean_config(text):
    return '\n'.join(line for line in text.splitlines()
                     if line.split('=', 1)[0].strip() not in PRIVATE_KEYS
                     and not line.startswith('# Development package:')) + '\n'


def run(*args):
    return subprocess.check_output(list(map(str, args)), text=True).strip()


def version_tuple(value):
    return tuple(map(int, value.split('.')))


def audit_binary(path, minimum):
    build = run('xcrun', 'vtool', '-show-build', path)
    versions = re.findall(r'\bminos\s+([\d.]+)', build)
    if not versions or any(version_tuple(v) > version_tuple(minimum) for v in versions):
        raise ValueError(f'{path.name}: deployment targets {versions} exceed macOS {minimum}')
    if 'arm64' not in run('lipo', '-archs', path):
        raise ValueError(f'{path.name}: missing arm64 architecture')
    return versions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--minimum-macos', default='14.0')
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'out/releases')
    parser.add_argument('--alloy-source', type=Path, default=ROOT.parent / 'xenia-alloy')
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', args.version):
        parser.error('Version must be a filename-safe identifier')
    if version_tuple(args.minimum_macos) < (14, 0):
        parser.error('The Metal package requires macOS 14.0 or later')
    source = args.build_dir.resolve()
    name = f'FM4-{args.version}-macos-arm64'
    package = args.output_dir.resolve() / name
    if package.exists():
        parser.error(f'Output already exists: {package}')
    for name_in in FILES:
        if not (source / name_in).is_file():
            parser.error(f'Missing required release file: {name_in}')
    package.mkdir(parents=True)
    for name_in in FILES:
        shutil.copy2(source / name_in, package / name_in)
    for optional in ('libTracyClient.dylib',):
        if (source / optional).is_file():
            shutil.copy2(source / optional, package / optional)
    vulkan = source / 'vulkan'
    for relative in ('lib/libMoltenVK.dylib', 'lib/libvulkan.1.dylib',
                     'share/vulkan/icd.d/MoltenVK_icd.json'):
        target = package / 'vulkan' / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(vulkan / relative, target)
    (package / 'fm4.toml').write_text(clean_config((package / 'fm4.toml').read_text()))
    notices = package / 'notices'
    notices.mkdir()
    roots = [('rexglue-sdk', ROOT / 'thirdparty/rexglue-sdk'),
             ('xenia-alloy', args.alloy_source),
             ('DirectXShaderCompiler', args.alloy_source / 'third_party/DirectXShaderCompiler-mac'),
             ('MetalShaderConverter', args.alloy_source / 'third_party/metal-shader-converter/include'),
             ('SPIRV-Cross', args.alloy_source / 'third_party/SPIRV-Cross'),
             ('imgui-launcher', ROOT / 'thirdparty/imgui'),
             ('tomlplusplus-launcher', ROOT / 'thirdparty/tomlplusplus')]
    sdk_thirdparty = ROOT / 'thirdparty/rexglue-sdk/thirdparty'
    roots += [(p.name, p) for p in sdk_thirdparty.iterdir() if p.is_dir()]
    for label, directory in roots:
        for path in directory.iterdir():
            if path.is_file() and path.name.upper().startswith(('LICENSE', 'COPYING', 'NOTICE', 'ACKNOWLEDGEMENT', 'THIRDPARTYNOTICES')):
                target = notices / label / path.name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)
    stb = (ROOT / 'thirdparty/stb/stb_image.h').read_text()
    (notices / 'stb-image.txt').write_text(stb[stb.rfind('/*'):])
    (notices / 'SOURCES.md').write_text(
        '# Corresponding dependency sources\n\n'
        'ReXGlue SDK: https://github.com/rexglue/rexglue-sdk/tree/c94f5ebdcb3c9d1a460ca48e04f9758448f8d518\n\n'
        'FM4 source and ordered SDK patches: https://github.com/DarthMDev/hl360-forza4/tree/macos-support\n\n'
        'Xenia Alloy and converter sources: https://github.com/Igggy-code/xenia-alloy\n\n'
        'FFmpeg is built into the replaceable librexruntime.dylib; rebuild the SDK with its '
        'FFmpeg sources and the supplied patches to modify/relink it. Keep license notices when redistributing.\n')
    binaries = [p for p, directory in fixture_entries(package)
                if not directory and (p.suffix == '.dylib' or p.name in ('fm4', 'fm4-launcher', 'fm4tool'))]
    names = {p.name for p in binaries}
    audit = {}
    for path in binaries:
        audit[str(path.relative_to(package))] = audit_binary(path, args.minimum_macos)
        for dependency in re.findall(r'^\s+(.*?) \(compatibility', run('otool', '-L', path), re.M):
            if dependency.startswith(('/usr/lib/', '/System/Library/')):
                continue
            if Path(dependency).name not in names:
                raise ValueError(f'{path.name}: unbundled dependency {dependency}')
            replacement = '@rpath/' + Path(dependency).name
            if dependency != replacement:
                run('install_name_tool', '-change', dependency, replacement, path)
        commands = run('otool', '-l', path)
        for rpath in dict.fromkeys(re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset', commands)):
            run('install_name_tool', '-delete_rpath', rpath, path)
        for rpath in ('@loader_path', '@loader_path/vulkan/lib'):
            run('install_name_tool', '-add_rpath', rpath, path)
        if path.suffix == '.dylib':
            run('install_name_tool', '-id', '@rpath/' + path.name, path)
        run('codesign', '--force', '--sign', '-', path)
    stage_launcher(package)
    for path in binaries:
        run('codesign', '--verify', '--strict', path)
    (package / 'metal-backend.json').write_text(json.dumps({
        'backend': 'metal', 'minimum_macos': args.minimum_macos,
        'project_revision': run('git', '-C', ROOT, 'rev-parse', 'HEAD'),
        'deployment_targets': audit}, indent=2) + '\n')
    (package / 'README.txt').write_text(
        f'FM4 {args.version} — Apple Silicon macOS {args.minimum_macos}+\n\n'
        'Extract the whole folder to a writable location. Keep all files together.\n'
        'Open FM4 Launcher.app, install your supported USA Rev 1 Disc 1 ISO, then Disc 2, and play.\n'
        'Game files, DLC, saves and personal settings are not included.\n'
        'Metal and 60 FPS are the defaults. 30 FPS mode retains normal game speed.\n'
        'The sun-glare workaround is enabled; relaxed GPU synchronization is not.\n'
        'Keep the user folder and fm4.user.toml when updating.\n'
        'Locally signed, not notarized. macOS may require approval in Privacy & Security.\n'
        'Built for macOS 14; gameplay tested on macOS 27. Older OS runtime testing is pending.\n'
        'Source and support: https://github.com/DarthMDev/hl360-forza4\n')
    archive = package.parent / (package.name + '.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as output:
        for path, directory in fixture_entries(package):
            if not directory:
                output.write(path, str(path.relative_to(package.parent)))
    checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_name(archive.name + '.sha256').write_text(checksum + '  ' + archive.name + '\n')
    print(archive)
    print(checksum)


if __name__ == '__main__':
    main()
