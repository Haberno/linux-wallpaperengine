"""Opt-in text property timeline regressions."""
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics tests')
class TextAnimation(unittest.TestCase):
    def test_current_property_animation_is_available_without_a_name(self):
        script = """
export function init() {
    thisObject.getAnimation().play();
    console.log('PROPERTY_ANIMATION_OK');
}
"""
        scene = {
            'camera': {'eye': '0 0 500', 'center': '0 0 0', 'up': '0 1 0'},
            'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                        'clearcolor': '0 0 0', 'camerafade': False},
            'objects': [{'id': 1, 'name': 'caption', 'text': 'Caption', 'pointsize': 32,
                         'origin': '160 90 0', 'size': '280 80',
                         'alpha': {'value': 0, 'script': script, 'animation': {
                             'c0': [{'frame': 0, 'value': 0}, {'frame': 2, 'value': 1}],
                             'options': {'fps': 10, 'length': 2, 'mode': 'single', 'startpaused': True}}}}]}
        with tempfile.TemporaryDirectory(prefix='lwe-default-property-') as directory:
            frame, output = render_scene(self, Path(directory), scene)
        self.assertIn('PROPERTY_ANIMATION_OK', output)
        self.assertGreater(frame.getextrema()[0][1], 250)

    def test_text_fields_are_available_to_dynamic_shadow_scripts(self):
        script = '''
export function init() {
    if (thisLayer.font !== 'systemfont_arial' || thisLayer.pointsize !== 32)
        throw Error('missing text fields');
    thisLayer.pointsize = 40;
    if (thisLayer.pointsize !== 40) throw Error('pointsize assignment failed');
    console.log('TEXT_FIELDS_OK');
}
'''
        scene = {
            'camera': {'eye': '0 0 500', 'center': '0 0 0', 'up': '0 1 0'},
            'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                        'clearcolor': '0 0 0', 'camerafade': False},
            'objects': [{'id': 1, 'name': 'text source', 'text': 'Shadow',
                         'font': 'systemfont_arial', 'pointsize': 32,
                         'origin': '160 90 0', 'size': '280 80',
                         'visible': {'value': True, 'script': script}}]}
        with tempfile.TemporaryDirectory(prefix='lwe-text-fields-') as directory:
            _, output = render_scene(self, Path(directory), scene)
        self.assertIn('TEXT_FIELDS_OK', output)

    def test_single_alpha_timeline_hides_author_caption(self):
        frames = []
        for animated in (False, True):
            alpha = {'value': 1, 'animation': {
                'c0': [{'frame': 0, 'value': 1}, {'frame': 2, 'value': 0}],
                'options': {'fps': 10, 'length': 2, 'mode': 'single'}}} if animated else 0
            scene = {
                'camera': {'eye': '0 0 500', 'center': '0 0 0', 'up': '0 1 0'},
                'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                            'clearcolor': '0 0 0', 'camerafade': False},
                'objects': [{'id': 1, 'name': 'author caption', 'text': 'By Author',
                             'font': 'systemfont_arial', 'pointsize': 32,
                             'origin': '160 90 0', 'size': '280 80', 'alpha': alpha}]}
            with tempfile.TemporaryDirectory(prefix='lwe-text-animation-') as directory:
                frame, _ = render_scene(self, Path(directory), scene)
                frames.append(frame)
        self.assertEqual(frames[0].tobytes(), frames[1].tobytes())


if __name__ == '__main__':
    unittest.main()
