#!/usr/bin/env python3
"""Validate the full native Metal game plugin without launching FM4."""
import argparse
import ctypes
from datetime import datetime, timezone
from pathlib import Path
import platform
import sys
from macos_files import copy_fixture_tree

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plugin', type=Path, default=ROOT / 'thirdparty/rexglue-sdk/out/mac-arm64/librexgpu-xenos.dylib')
    parser.add_argument('--shader-cache', type=Path)
    parser.add_argument('--window', action='store_true', help='Run a standalone native window and three resize checks')
    parser.add_argument('--pipeline-cache-fixture', type=Path,
                        help='Validate blocking startup prewarm using a private copy of this cache')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        parser.error('Use native Apple Silicon macOS')
    if not args.plugin.is_file() or (not args.window and (args.shader_cache is None or not args.shader_cache.is_file())):
        parser.error('Plugin and read-only shader cache must exist')
    output = args.output or ROOT / 'out/macos/metal-runtime-check' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        parser.error('Output must be a new directory')
    draw_output = output
    if args.pipeline_cache_fixture:
        if args.window or not args.pipeline_cache_fixture.is_dir():
            parser.error('Pipeline fixture must be an existing directory; cannot combine with --window')
        copy_fixture_tree(args.pipeline_cache_fixture, output / 'cache')
        draw_output = output / 'draw-check'
    library = ctypes.CDLL(str(args.plugin.resolve()))
    library.rex_gpu_abi_version.restype = ctypes.c_uint32
    if library.rex_gpu_abi_version() != 1:
        raise RuntimeError('Unexpected GPU plugin ABI')
    if args.pipeline_cache_fixture:
        prewarm = library.rex_gpu_metal_prewarm_validate
        prewarm.argtypes = [ctypes.c_char_p]
        prewarm.restype = ctypes.c_int
        if prewarm(str((output / 'cache').resolve()).encode()):
            return 1
    if args.window:
        validate_window = library.rex_gpu_metal_window_validate
        validate_window.argtypes = []
        validate_window.restype = ctypes.c_int
        return validate_window()
    validate = library.rex_gpu_metal_validate
    validate.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    validate.restype = ctypes.c_int
    result = validate(str(args.shader_cache.resolve()).encode(), str(draw_output).encode())
    print(f'Full Metal backend results: {output}', flush=True)
    return result


if __name__ == '__main__':
    sys.exit(main())
