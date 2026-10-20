import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from mac_cache import latest_metal_cache


class MetalCacheTests(unittest.TestCase):
    def test_only_completed_compatible_caches_are_reused(self):
        with tempfile.TemporaryDirectory() as directory:
            runs = Path(directory)
            base = dict(backend="metal", process_state="exited", exit_code=0,
                        running_at_timeout=False, xex_sha256="game",
                        libraries={"librexgpu-xenos.dylib": "plugin"})
            changes = [{}, {"process_state": "running"}, {"exit_code": 1},
                       {"running_at_timeout": True}, {"xex_sha256": "other"},
                       {"libraries": {"librexgpu-xenos.dylib": "other"}},
                       {"backend": "vulkan"}, {"process_state": "prepared"}]
            for index, change in enumerate(changes):
                run = runs / str(index)
                cache = run / "cache/shaders/metal/local"
                cache.mkdir(parents=True)
                (cache / "shader.metalshcache").write_bytes(b"cache")
                (run / "receipt.json").write_text(json.dumps(base | change))
            self.assertEqual(latest_metal_cache(runs, "plugin", "game"), runs / "0/cache")
            (runs / "9").mkdir()
            (runs / "9/receipt.json").write_text("invalid")
            self.assertEqual(latest_metal_cache(runs, "plugin", "game"), runs / "0/cache")
            self.assertIsNone(latest_metal_cache(runs, "missing", "game"))
            self.assertIsNone(latest_metal_cache(runs, None, "game"))

    def test_missing_or_empty_cache_is_not_selected(self):
        with tempfile.TemporaryDirectory() as directory:
            runs = Path(directory)
            self.assertIsNone(latest_metal_cache(runs / "absent", "p", "g"))
            run = runs / "1"
            run.mkdir()
            (run / "receipt.json").write_text(json.dumps(dict(backend="metal",
                process_state="exited", exit_code=0, xex_sha256="g",
                libraries={"librexgpu-xenos.dylib": "p"})))
            self.assertIsNone(latest_metal_cache(runs, "p", "g"))


if __name__ == "__main__":
    unittest.main()
