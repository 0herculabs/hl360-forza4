import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
spec = importlib.util.spec_from_file_location('clean_mac', Path(__file__).resolve().parents[1] / 'tools/Clean-Mac.py')
clean = importlib.util.module_from_spec(spec)
spec.loader.exec_module(clean)


class CleanupTests(unittest.TestCase):
    def test_compaction_preserves_changed_saves_and_manifest_on_repeat(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline, run = root / 'baseline', root / 'run'
            baseline.mkdir()
            (run / 'user').mkdir(parents=True)
            (baseline / 'dlc').write_bytes(b'identical')
            (run / 'user/dlc').write_bytes(b'identical')
            (baseline / 'save').write_bytes(b'old save')
            (run / 'user/save').write_bytes(b'new save')
            clean.compact_profile(run, baseline, {}, True)
            self.assertFalse((run / 'user/dlc').exists())
            self.assertEqual((run / 'user/save').read_bytes(), b'new save')
            clean.compact_profile(run, baseline, {}, True)
            manifest = json.loads((run / 'profile-compaction.json').read_text())
            self.assertIn('dlc', manifest['duplicate_files'])
            self.assertIn('save', manifest['retained_changed_or_unique_files'])
            self.assertEqual((baseline / 'dlc').read_bytes(), b'identical')


if __name__ == '__main__':
    unittest.main()
