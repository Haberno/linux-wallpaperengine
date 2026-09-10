"""Opt-in regression for cameras moving through a 2D authoring canvas."""
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class CameraCanvas(unittest.TestCase):
    def test_camera_pan_selects_the_authored_side_of_the_canvas(self):
        with tempfile.TemporaryDirectory(prefix='lwe-camera-canvas-') as directory:
            for y, expected in ((60, (255, 0, 0)), (-60, (0, 0, 255))):
                with self.subTest(y=y):
                    frame, _ = render_scene(self, Path(directory) / str(y), {
                        'camera': {'eye': '0 0 500', 'center': '0 0 0', 'up': '0 1 0'},
                        'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                                    'clearcolor': '0 0 0', 'bloom': False},
                        'objects': [
                            {'id': 1, 'image': 'models/util/solidlayer.json',
                             'origin': '160 150 0', 'size': '320 60', 'color': '1 0 0'},
                            {'id': 2, 'image': 'models/util/solidlayer.json',
                             'origin': '160 30 0', 'size': '320 60', 'color': '0 0 1'},
                            {'id': 3, 'camera': 'default', 'origin': f'0 {y} 500',
                             'zoom': 3, 'visible': True}]})
                    self.assertEqual(frame.getpixel((160, 90)), expected,
                                     'A positive camera Y must pan toward the upper canvas')


if __name__ == '__main__':
    unittest.main()
