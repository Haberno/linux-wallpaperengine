"""Opt-in regressions for omitted particle settings."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticleDefaults(unittest.TestCase):
    def test_attraction_defaults_follow_the_scene_units(self):
        from PIL import ImageChops

        # Original engine: FUN_1401bdee0 selects these scale/radius defaults from
        # the orthographic scene flag (set while reading orthogonalprojection).
        # Compare full rendered trajectories with explicitly authored settings,
        # including null values, and ensure the probe actually exercises motion.
        with tempfile.TemporaryDirectory(prefix='lwe-attraction-defaults-') as directory:
            for perspective in (False, True):
                frames = {}
                for mode in ('explicit', 'omitted', 'null'):
                    with self.subTest(perspective=perspective, mode=mode):
                        root = Path(directory) / f'{perspective}-{mode}'
                        (root / 'particles').mkdir(parents=True)
                        (root / 'materials').mkdir()
                        attract = {'name': 'controlpointattract', 'controlpoint': 1}
                        if mode == 'explicit':
                            attract.update(scale=20 if perspective else 512,
                                           threshold=5 if perspective else 512)
                        elif mode == 'null':
                            attract.update(scale=None, threshold=None)
                        size = .5 if perspective else 4
                        (root / 'particles/probe.json').write_text(json.dumps({
                            'maxcount': 1, 'material': 'materials/probe.json',
                            'controlpoint': [{'id': 0, 'flags': 0, 'offset': '0 0 0'},
                                             {'id': 1, 'flags': 0,
                                              'offset': '4 0 0' if perspective else '120 0 0'}],
                            'emitter': [{'name': 'sphererandom', 'rate': 100,
                                         'distancemin': 0, 'distancemax': 0}],
                            'initializer': [{'name': 'lifetimerandom', 'min': 10, 'max': 10},
                                            {'name': 'sizerandom', 'min': size, 'max': size}],
                            'operator': [attract, {'name': 'movement', 'gravity': '0 0 0', 'drag': 0}],
                            'renderer': [{'name': 'sprite'}]}))
                        (root / 'materials/probe.json').write_text(json.dumps({'passes': [{
                            'shader': 'genericparticle', 'textures': ['util/white'],
                            'blending': 'normal', 'cullmode': 'nocull',
                            'depthtest': 'disabled', 'depthwrite': 'disabled'}]}))
                        frame, _ = render_scene(self, root, {
                            'camera': {'eye': '0 0 10', 'center': '0 0 0', 'up': '0 1 0'},
                            'general': {'orthogonalprojection': None if perspective else
                                        {'width': 320, 'height': 180},
                                        'fov': 50, 'nearz': .1, 'farz': 1000,
                                        'clearcolor': '0 0 0', 'bloom': False},
                            'objects': [{'id': 1, 'name': 'Attraction probe',
                                         'origin': '0 0 0' if perspective else '160 90 0',
                                         'particle': 'particles/probe.json'}]}, frames=5)
                        frames[mode] = frame
                        box = frame.getbbox()
                        self.assertIsNotNone(box)
                        self.assertGreater(box[0], 164, 'The attractor must visibly move the particle')
                for mode in ('omitted', 'null'):
                    self.assertIsNone(ImageChops.difference(frames['explicit'], frames[mode]).getbbox(),
                                      f'{perspective=}, {mode=} should follow the authored-default trajectory')

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
