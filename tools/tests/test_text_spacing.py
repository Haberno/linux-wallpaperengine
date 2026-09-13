"""Opt-in native text-spacing regressions for the glyph and effect paths."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_text_effect_targets import base_scene, write_copy_assets


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics tests')
class TextSpacing(unittest.TestCase):
    def render_text(self, root, pointsize=12, spacing=None, effect=False):
        text = {'id': 1, 'name': 'Spaced text', 'text': 'AAAA\nAA',
                'font': 'fonts/RobotoMono-Regular.ttf', 'pointsize': pointsize,
                'origin': '20 160 0', 'horizontalalign': 'left', 'verticalalign': 'top',
                'padding': '8 16'}
        if spacing is not None:
            text['spacing'] = spacing
        if effect:
            write_copy_assets(root)
            (root / 'effects/copy.json').write_text(json.dumps({
                'passes': [{'material': 'materials/copy.json'}]}))
            text['effects'] = [{'id': 2, 'file': 'effects/copy.json'}]
        scene = base_scene([text])
        scene['general']['camerafade'] = False
        frame, _ = render_scene(self, root, scene)
        bounds = frame.getbbox()
        self.assertIsNotNone(bounds, 'The stock Roboto glyphs must render')
        self.assertGreater(bounds[0], 0)
        self.assertGreater(bounds[1], 0)
        self.assertLess(bounds[2], frame.width)
        self.assertLess(bounds[3], frame.height)
        return frame, (bounds[2] - bounds[0], bounds[3] - bounds[1])

    def test_spacing_adds_pixels_independently_of_pointsize(self):
        # Native FUN_1401b0410 adds spacing.x to each glyph advance and spacing.y
        # to the FreeType row height. Four glyphs have three visible X gaps.
        with tempfile.TemporaryDirectory(prefix='lwe-text-spacing-units-') as directory:
            for size in (6, 12):
                _, baseline = self.render_text(Path(directory) / f'{size}-zero', size, '0 0')
                for spacing, delta in (('10 0', (30, 0)), ('-2 0', (-6, 0)),
                                       ('0 10', (0, 10)), ('0 -2', (0, -2))):
                    with self.subTest(pointsize=size, spacing=spacing):
                        _, actual = self.render_text(Path(directory) / f'{size}-{spacing}', size, spacing)
                        self.assertEqual(actual, (baseline[0] + delta[0], baseline[1] + delta[1]))

    def test_zero_preserves_pixels_and_fractional_tracking_accumulates(self):
        with tempfile.TemporaryDirectory(prefix='lwe-text-spacing-fraction-') as directory:
            baseline, bounds = self.render_text(Path(directory) / 'omitted')
            zero, _ = self.render_text(Path(directory) / 'zero', spacing='0 0')
            fractional, actual = self.render_text(Path(directory) / 'fraction', spacing='0.5 0.5')
            self.assertEqual(baseline.tobytes(), zero.tobytes())
            self.assertGreater(actual[0], bounds[0])
            self.assertNotEqual(baseline.tobytes(), fractional.tobytes())

    def test_script_and_animation_rebuild_glyphs_and_effect_surfaces(self):
        script = {'value': '0 0', 'script': '''
export function update(value) {
    return engine.runtime > 0.15 ? new Vec2(10, 20) : value;
}
'''}
        animation = {'value': '0 0', 'animation': {
            'c0': [{'frame': 0, 'value': 0}, {'frame': 2, 'value': 10}],
            'c1': [{'frame': 0, 'value': 0}, {'frame': 2, 'value': 20}],
            'options': {'fps': 10, 'length': 2, 'mode': 'single'}}}
        with tempfile.TemporaryDirectory(prefix='lwe-text-spacing-dynamic-') as directory:
            for effect in (False, True):
                expected, _ = self.render_text(Path(directory) / f'{effect}-static',
                                               spacing='10 20', effect=effect)
                for name, spacing in (('script', script), ('animation', animation)):
                    with self.subTest(effect=effect, source=name):
                        actual, _ = self.render_text(Path(directory) / f'{effect}-{name}',
                                                     spacing=spacing, effect=effect)
                        self.assertEqual(actual.tobytes(), expected.tobytes())


if __name__ == '__main__':
    unittest.main()
