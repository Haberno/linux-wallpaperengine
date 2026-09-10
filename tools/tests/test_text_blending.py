"""Opt-in text layer blending against the scene, with and without effects."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_text_effect_targets import base_scene, write_copy_assets


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class TextBlending(unittest.TestCase):
    def test_overlay_uses_background_color_and_applies_opacity_once(self):
        with tempfile.TemporaryDirectory(prefix='lwe-text-blending-') as directory:
            for with_effect in (False, True):
                for alpha in (1, .5):
                    with self.subTest(effect=with_effect, alpha=alpha):
                        root = Path(directory) / f'effect-{with_effect}-alpha-{alpha}'
                        write_copy_assets(root)
                        (root / 'effects/copy.json').write_text(json.dumps({
                            'passes': [{'material': 'materials/copy.json'}]}))
                        text = {'id': 1, 'name': 'Overlay', 'text': 'M', 'pointsize': 24,
                                'origin': '160 90 0', 'colorBlendMode': 11, 'alpha': alpha}
                        if with_effect:
                            text['effects'] = [{'id': 2, 'file': 'effects/copy.json'}]
                        scene = base_scene([text])
                        scene['general']['clearcolor'] = '.2 .3 .4'
                        frame, _ = render_scene(self, root, scene)
                        # Overlaying white doubles channels below 0.5. Glyph coverage
                        # and the layer alpha then interpolate with the original scene.
                        actual = max(frame.getdata(), key=lambda pixel: pixel[0])
                        expected = tuple(round(255 * value * (1 + alpha)) for value in (.2, .3, .4))
                        for value, reference in zip(actual, expected):
                            self.assertAlmostEqual(value, reference, delta=2,
                                                   msg=f'Got {actual}, expected {expected}')
                        self.assertEqual(frame.getpixel((0, 0)), (51, 76, 102))


if __name__ == '__main__':
    unittest.main()
