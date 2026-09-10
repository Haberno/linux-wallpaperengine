"""Opt-in regression for an omitted particle velocity range."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticleDefaults(unittest.TestCase):
    def test_omitted_velocity_does_not_scatter_bubbles(self):
        with tempfile.TemporaryDirectory(prefix='lwe-particle-defaults-') as directory:
            root = Path(directory)
            (root / 'particles').mkdir()
            (root / 'materials').mkdir()
            (root / 'particles/probe.json').write_text(json.dumps({
                'maxcount': 50, 'material': 'materials/probe.json',
                'emitter': [{'name': 'sphererandom', 'rate': 50,
                             'distancemin': 0, 'distancemax': 0}],
                'initializer': [{'name': 'lifetimerandom', 'min': 10, 'max': 10},
                                {'name': 'sizerandom', 'min': 5, 'max': 5},
                                {'name': 'velocityrandom'}],
                'operator': [{'name': 'movement', 'gravity': '0 0 0', 'drag': 0}],
                'renderer': [{'name': 'sprite'}]}))
            (root / 'materials/probe.json').write_text(json.dumps({'passes': [{
                'shader': 'genericparticle', 'textures': ['util/white'],
                'blending': 'normal', 'cullmode': 'nocull',
                'depthtest': 'enabled', 'depthwrite': 'disabled'}]}))
            frame, _ = render_scene(self, root, {
                'camera': {'eye': '0 0 100', 'center': '0 0 0', 'up': '0 1 0'},
                'general': {'orthogonalprojection': None, 'fov': 50,
                            'clearcolor': '0 0 0', 'bloom': False},
                'objects': [{'id': 1, 'name': 'Bubbles', 'particle': 'particles/probe.json'}]}, frames=15)
            box = frame.getbbox()
            self.assertIsNotNone(box)
            self.assertLess(box[2] - box[0], 10)
            self.assertLess(box[3] - box[1], 10)
            self.assertAlmostEqual((box[0] + box[2]) / 2, 160, delta=1)
            self.assertAlmostEqual((box[1] + box[3]) / 2, 90, delta=1)


if __name__ == '__main__':
    unittest.main()
