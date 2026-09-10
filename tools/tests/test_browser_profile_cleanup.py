"""Opt-in regression for temporary CEF profile lifetime."""
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from scene_render import render_scene
from test_text_effect_targets import base_scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class BrowserProfileCleanup(unittest.TestCase):
    def test_shutdown_removes_its_profile_and_preserves_other_temporary_files(self):
        # Chromium appends its singleton socket name beneath TMPDIR. Keep this
        # path short even when the outer graphics harness has a long TMPDIR.
        with tempfile.TemporaryDirectory(prefix='lwe-b-', dir='/tmp') as directory:
            root = Path(directory)
            profiles = root / 'p'
            profiles.mkdir()
            unrelated = profiles / 'lwe-cef-unrelated'
            unrelated.mkdir()
            (unrelated / 'keep').write_text('preserve this')
            with patch.dict(os.environ, {'TMPDIR': str(profiles)}):
                for launch in range(2):
                    _, log = render_scene(self, root / f'launch-{launch}', base_scene([
                        {'id': 1, 'name': 'Text', 'text': 'Browser cleanup',
                         'origin': '160 90 0', 'pointsize': 12}]))
                    self.assertIn('Shutting down CEF', log)
                    self.assertEqual(list(profiles.iterdir()), [unrelated],
                                     'Each stopped engine must remove its own temporary browser profile')
            self.assertEqual((unrelated / 'keep').read_text(), 'preserve this')


if __name__ == '__main__':
    unittest.main()
