import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('mac_package', Path(__file__).parents[1] / 'tools/Package-Mac.py')
module = importlib.util.module_from_spec(spec)
import sys
sys.path.insert(0, str(Path(__file__).parents[1] / 'tools'))
spec.loader.exec_module(module)


class ReleasePackagingTest(unittest.TestCase):
    def test_personal_paths_and_profile_excluded(self):
        config = module.clean_config('gpu_backend = "metal"\nuser_data_root = "/private/save"\n'
                                     'game_data_root = "/private/discs"\nprofile_xuid = "1234"\n'
                                     'profile_name = "Personal"\nframe_samples = "/private/frames"\n'
                                     'fm4_frame_limit = 0\nquery_occlusion_fake_sample_count = 0\n')
        self.assertNotIn('/private', config)
        self.assertNotIn('profile_', config)
        self.assertIn('gpu_backend = "metal"', config)
        self.assertIn('query_occlusion_fake_sample_count = 0', config)

    def test_newer_dependency_rejected(self):
        with patch.object(module, 'run', return_value='platform MACOS\nminos 27.0\nsdk 27.0'):
            with self.assertRaises(ValueError):
                module.audit_binary(Path('dependency.dylib'), '14.0')
        with patch.object(module, 'run', side_effect=['platform MACOS\nminos 14.0\nsdk 27.0', 'arm64']):
            self.assertEqual(module.audit_binary(Path('game'), '14.0'), ['14.0'])
