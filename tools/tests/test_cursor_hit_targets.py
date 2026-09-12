"""Opt-in regressions for invisible and flattened script interaction layers."""
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics tests')
class CursorHitTargets(unittest.TestCase):
    def test_cursor_events_reach_invisible_and_zero_depth_images(self):
        for visible, scale in ((False, '1 1 1'), (True, '1 1 0')):
            with self.subTest(visible=visible, scale=scale):
                script = '''
let entered = false;
export function init() { thisLayer.origin = input.cursorWorldPosition; }
export function cursorEnter(event) { entered = true; }
export function update(value) {
    if (engine.runtime > .2) {
        if (!entered) throw Error('cursor target rejected');
        console.log('CURSOR_TARGET_OK');
    }
    return value;
}
'''
                scene = {
                    'camera': {'eye': '0 0 500', 'center': '0 0 0', 'up': '0 1 0'},
                    'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                                'clearcolor': '0 0 0', 'camerafade': False},
                    'objects': [{'id': 1, 'name': 'cursor target', 'image': 'models/util/solidlayer.json',
                                 'origin': '160 90 0', 'size': '40 40', 'scale': scale,
                                 'visible': {'value': visible, 'script': script}}]}
                with tempfile.TemporaryDirectory(prefix='lwe-cursor-target-') as directory:
                    _, output = render_scene(self, Path(directory), scene)
                self.assertIn('CURSOR_TARGET_OK', output)


if __name__ == '__main__':
    unittest.main()
