"""Select a completed, compatible Metal cache without modifying its source."""
import json
from macos_files import fixture_entries


def latest_metal_cache(runs, plugin_sha256, xex_sha256):
    if not plugin_sha256 or not xex_sha256:
        return None
    for run in sorted(runs.iterdir(), reverse=True) if runs.is_dir() else ():
        try:
            receipt = json.loads((run / "receipt.json").read_text())
            if (receipt.get("backend") != "metal"
                    or receipt.get("process_state") != "exited"
                    or receipt.get("exit_code") != 0
                    or receipt.get("running_at_timeout")
                    or receipt.get("libraries", {}).get("librexgpu-xenos.dylib") != plugin_sha256
                    or receipt.get("xex_sha256") != xex_sha256):
                continue
            cache = run / "cache"
            if any(not is_dir and path.suffix == ".metalshcache"
                   for path, is_dir in fixture_entries(cache / "shaders/metal")):
                return cache
        except (OSError, ValueError, TypeError, AttributeError):
            continue
    return None
