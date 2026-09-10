"""Opt-in 3D particle placement and camera-facing sprite regression."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticleWorld(unittest.TestCase):
    def test_parented_sprites_face_the_actual_scene_camera(self):
        with tempfile.TemporaryDirectory(prefix='lwe-particle-world-') as directory:
            root = Path(directory)
            for flags in (0, 4):
                fixture = root / f'flags-{flags}'
                (fixture / 'particles').mkdir(parents=True)
                (fixture / 'materials').mkdir()
                (fixture / 'particles/probe.json').write_text(json.dumps({
                    'flags': flags, 'maxcount': 10, 'material': 'materials/probe.json',
                    'emitter': [{'name': 'sphererandom', 'rate': 10, 'distancemin': 0,
                                 'distancemax': 0}],
                    'initializer': [{'name': 'lifetimerandom', 'min': 10, 'max': 10},
                                    {'name': 'sizerandom', 'min': 20, 'max': 20},
                                    {'name': 'velocityrandom', 'min': '0 0 0', 'max': '0 0 0'}],
                    'renderer': [{'name': 'sprite'}]}))
                (fixture / 'materials/probe.json').write_text(json.dumps({'passes': [{
                    'shader': 'genericparticle', 'textures': ['util/white'],
                    'blending': 'normal', 'cullmode': 'nocull',
                    'depthtest': 'disabled', 'depthwrite': 'disabled'}]}))
                image, _ = render_scene(self, fixture, {
                    'camera': {'eye': '60 30 80', 'center': '0 0 0', 'up': '0 1 0'},
                    'general': {'orthogonalprojection': None, 'fov': 50,
                                'clearcolor': '0 0 0', 'bloom': False},
                    'objects': [
                        {'id': 1, 'name': 'Rotated parent', 'solid': True,
                         'angles': '.4 1.5708 .6', 'scale': '.5 .5 .5'},
                        {'id': 2, 'name': 'Bubble', 'parent': 1,
                         'particle': 'particles/probe.json', 'origin': '0 0 0'}]})
                box = image.point(lambda value: 255 if value > 50 else 0).getbbox()
                self.assertIsNotNone(box, 'The 3D emitter must be visible')
                self.assertAlmostEqual((box[0] + box[2]) / 2, 160, delta=1)
                self.assertAlmostEqual((box[1] + box[3]) / 2, 90, delta=1)
                self.assertGreater(box[2] - box[0], 8, 'Parent rotation must not turn the sprite edge-on')
                self.assertAlmostEqual(box[2] - box[0], box[3] - box[1], delta=2)


if __name__ == '__main__':
    unittest.main()
