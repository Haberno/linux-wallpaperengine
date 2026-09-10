"""Opt-in regression for color-based effects sampling rasterized glyphs."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_text_effect_targets import base_scene, write_copy_assets


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class TextEffectCoverage(unittest.TestCase):
    def test_color_effects_preserve_spaces_and_glyph_counters(self):
        with tempfile.TemporaryDirectory(prefix='lwe-text-coverage-') as directory:
            frames = []
            for with_effect in (False, True):
                root = Path(directory) / f'color-effect-{with_effect}'
                write_copy_assets(root)
                # Shine and other color-based effects can derive opacity from RGB.
                # Expose the input color directly so invisible white in a glyph's
                # counters or between characters becomes a visible regression.
                (root / 'shaders/copy.frag').write_text('''
uniform sampler2D g_Texture0;
varying vec2 v_TexCoord;
void main() { gl_FragColor = vec4(texSample2D(g_Texture0, v_TexCoord).rgb, 1.0); }
''')
                (root / 'effects/color.json').write_text(json.dumps({
                    'passes': [{'material': 'materials/copy.json'}]}))
                text = {'id': 1, 'name': 'Hollow glyphs', 'text': 'O O',
                        'pointsize': 20, 'color': '1 .25 .5', 'origin': '160 90 0'}
                if with_effect:
                    text['effects'] = [{'id': 2, 'file': 'effects/color.json'}]
                frame, _ = render_scene(self, root, base_scene([text]))
                frames.append(frame)
            self.assertGreater(sum(pixel[0] > 200 for pixel in frames[0].getdata()), 100)
            for original, effect in zip(frames[0].getdata(), frames[1].getdata()):
                if original[0] == 0:
                    self.assertLessEqual(max(effect), 2,
                                         'Color effects must not fill transparent glyph areas')
            for channel in range(3):
                self.assertAlmostEqual(max(p[channel] for p in frames[0].getdata()),
                                       max(p[channel] for p in frames[1].getdata()), delta=2)


if __name__ == '__main__':
    unittest.main()
