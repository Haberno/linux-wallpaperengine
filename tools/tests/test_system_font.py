"""Authored system font families must select the same face as an embedded font."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from scene_render import render_scene
from test_text_effect_targets import base_scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY') and shutil.which('fc-match'),
                     'Set LWE_TEST_BINARY and install Fontconfig for graphics tests')
class SystemFont(unittest.TestCase):
    def test_system_family_uses_the_fontconfig_selected_face(self):
        font = Path(subprocess.check_output(
            ['fc-match', '-f', '%{file}', 'DejaVu Serif'], text=True))
        if not font.is_file():
            self.skipTest('No serif font installed')
        with tempfile.TemporaryDirectory(prefix='lwe-system-font-') as directory:
            root = Path(directory)
            (root / 'fonts').mkdir()
            shutil.copyfile(font, root / 'fonts/reference.ttf')
            frames = []
            for name in ('systemfont_DejaVu Serif', 'fonts/reference.ttf'):
                (root / 'frame.png').unlink(missing_ok=True)
                frame, _ = render_scene(self, root, base_scene([{
                    'id': 1, 'name': 'Song title', 'text': 'Song of Time', 'font': name,
                    'pointsize': 14, 'origin': '160 90 0'}]))
                frames.append(frame)
            self.assertIsNotNone(frames[0].getbbox())
            self.assertEqual(frames[0].tobytes(), frames[1].tobytes())


if __name__ == '__main__':
    unittest.main()
