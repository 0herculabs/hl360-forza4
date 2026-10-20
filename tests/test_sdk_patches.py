import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("sdk_patches", Path(__file__).parents[1] / "tools/sdk_patches.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class PatchStackTest(unittest.TestCase):
    def test_new_file_extended_by_later_patch_and_idempotence(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); source = root / "source"; source.mkdir()
            first = root / "1.patch"; second = root / "2.patch"
            first.write_text("diff --git a/new.txt b/new.txt\nnew file mode 100644\n--- /dev/null\n+++ b/new.txt\n@@ -0,0 +1 @@\n+first\n")
            second.write_text("diff --git a/new.txt b/new.txt\n--- a/new.txt\n+++ b/new.txt\n@@ -1 +1,2 @@\n first\n+second\n")
            (source / "unrelated.txt").write_text("keep my work\n")
            self.assertEqual(module.apply_sdk_patches(source, [first]), 0)
            self.assertEqual(module.apply_sdk_patches(source, [first, second]), 1)
            self.assertEqual(module.apply_sdk_patches(source, [first, second]), 2)
            self.assertEqual((source / "new.txt").read_text(), "first\nsecond\n")
            self.assertEqual((source / "unrelated.txt").read_text(), "keep my work\n")
            (source / "new.txt").write_text("conflicting local edit\n")
            with self.assertRaises(ValueError): module.apply_sdk_patches(source, [first, second])
            self.assertEqual((source / "new.txt").read_text(), "conflicting local edit\n")
