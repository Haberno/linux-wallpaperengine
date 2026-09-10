"""Opt-in image opacity/tint preservation through generated blend passes."""
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ImageCompositeTint(unittest.TestCase):
    def test_blending_applies_layer_tint_and_opacity_once(self):
        with tempfile.TemporaryDirectory(prefix='lwe-composite-tint-') as directory:
            for label, color, alpha, expected in (
                    ('faint-shadow', '0 0 0', .13, (222, 222, 222)),
                    ('colored-layer', '.8 .4 .2', 1, (204, 102, 51))):
                with self.subTest(label=label):
                    frame, _ = render_scene(self, Path(directory) / label, {
                        'camera': {'eye': '0 0 500', 'center': '0 0 0', 'up': '0 1 0'},
                        'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                                    'clearcolor': '1 1 1', 'bloom': False},
                        'objects': [{'id': 1, 'image': 'models/util/solidlayer.json',
                                     'origin': '160 90 0', 'size': '100 100',
                                     'color': color, 'alpha': alpha, 'colorBlendMode': 2}]})
                    actual = frame.getpixel((160, 90))
                    for value, reference in zip(actual, expected):
                        self.assertAlmostEqual(value, reference, delta=2,
                                               msg=f'{label}: got {actual}, expected {expected}')


if __name__ == '__main__':
    unittest.main()
