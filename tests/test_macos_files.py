import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from macos_files import copy_fixture_file, copy_fixture_tree, fixture_entries, remove_fixture_tree, isolate_thumbnail_copies


class FixtureCopyTests(unittest.TestCase):
    def test_thumbnail_backup_preserves_original_and_other_saves(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'original'
            profile = source / 'XUID/4D530910/00000001/ForzaProfile'
            thumbs = profile / 'Thumbnails'
            thumbs.mkdir(parents=True)
            for name in ('Thumbnail_1.xdc', 'Thumbnail_3.xdc'):
                (thumbs / name).write_bytes(name.encode())
            (profile / 'PlayerDatabase').write_bytes(b'player progress')
            (profile / 'Other.xdc').write_bytes(b'unrelated')
            private, backup = Path(directory) / 'private', Path(directory) / 'backup'
            copy_fixture_tree(source, private)
            moved = isolate_thumbnail_copies(private, backup)
            self.assertEqual(len(moved), 2)
            for relative, checksum in moved.items():
                self.assertTrue((source / relative).is_file())
                self.assertFalse((private / relative).exists())
                self.assertEqual(hashlib.sha256((backup / relative).read_bytes()).hexdigest(), checksum)
            self.assertEqual((private / 'XUID/4D530910/00000001/ForzaProfile/PlayerDatabase').read_bytes(), b'player progress')
            self.assertTrue((private / 'XUID/4D530910/00000001/ForzaProfile/Other.xdc').is_file())

    def test_buffered_copy_hash_progress_and_independence(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'source'
            target = Path(directory) / 'target'
            payload = bytes(range(256)) * 40000
            source.write_bytes(payload)
            digest = hashlib.sha256()
            progress = []
            with patch('macos_files.platform.system', return_value='Linux'):
                copy_fixture_file(source, target, hasher=digest, progress=progress.append)
            self.assertEqual(target.read_bytes(), payload)
            self.assertEqual(digest.hexdigest(), hashlib.sha256(payload).hexdigest())
            self.assertEqual(sum(progress), len(payload))
            self.assertGreater(len(progress), 1)
            target.write_bytes(b'changed')
            self.assertEqual(source.read_bytes(), payload)

    def test_same_file_rejected_without_truncation(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'source'
            source.write_bytes(b'original')
            with self.assertRaises(OSError):
                copy_fixture_file(source, source)
            self.assertEqual(source.read_bytes(), b'original')

    def test_nested_tree_copy_and_removal_uses_stat(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'source'
            (source / 'nested/empty').mkdir(parents=True)
            (source / 'nested/save').write_bytes(b'keep this save')
            target = Path(directory) / 'copy'
            copy_fixture_tree(source, target)
            self.assertEqual((target / 'nested/save').read_bytes(), b'keep this save')
            self.assertTrue((target / 'nested/empty').is_dir())
            self.assertEqual(len(list(fixture_entries(target))), 3)
            remove_fixture_tree(target)
            self.assertFalse(target.exists())
            self.assertEqual((source / 'nested/save').read_bytes(), b'keep this save')

    def test_linked_payload_is_not_removed(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'source'
            source.mkdir()
            link = Path(directory) / 'link'
            link.symlink_to(source, target_is_directory=True)
            with self.assertRaises(ValueError):
                remove_fixture_tree(link)
            self.assertTrue(source.is_dir())


if __name__ == '__main__':
    unittest.main()
