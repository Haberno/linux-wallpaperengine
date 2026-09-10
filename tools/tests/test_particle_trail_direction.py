"""Opt-in regression for the authored head-to-tail direction of rope textures."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticleTrailDirection(unittest.TestCase):
    def test_rope_width_is_centered_on_the_particle_path(self):
        with tempfile.TemporaryDirectory(prefix='lwe-trail-center-') as directory:
            root = Path(directory)
            (root / 'particles').mkdir()
            (root / 'materials').mkdir()
            (root / 'materials/probe.json').write_text(json.dumps({'passes': [{
                'shader': 'genericparticle', 'textures': ['util/white'],
                'blending': 'normal', 'cullmode': 'nocull',
                'depthtest': 'disabled', 'depthwrite': 'disabled'}]}))
            (root / 'particles/probe.json').write_text(json.dumps({
                'maxcount': 1, 'material': 'materials/probe.json',
                'emitter': [{'name': 'sphererandom', 'rate': 100,
                             'distancemin': 0, 'distancemax': 0}],
                'initializer': [{'name': 'lifetimerandom', 'min': 10, 'max': 10},
                                {'name': 'sizerandom', 'min': 40, 'max': 40},
                                {'name': 'velocityrandom', 'min': '100 0 0', 'max': '100 0 0'}],
                'operator': [{'name': 'movement', 'gravity': '0 0 0', 'drag': 0}],
                'renderer': [{'name': 'ropetrail', 'length': .5}]}))
            frame, _ = render_scene(self, root, {
                'camera': {'eye': '0 0 1', 'center': '0 0 0', 'up': '0 1 0'},
                'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                            'clearcolor': '0 0 0', 'bloom': False},
                'objects': [{'id': 1, 'name': 'Centered trail', 'origin': '70 90 0',
                             'particle': 'particles/probe.json'}]}, frames=15)
            box = frame.getbbox()
            self.assertIsNotNone(box)
            self.assertAlmostEqual(box[3] - box[1], 40, delta=1)
            self.assertAlmostEqual((box[1] + box[3]) / 2, 90, delta=1,
                                   msg='Both edges must straddle the path, as in the native geometry shader')

    def test_shaft_trail_keeps_its_bright_head_in_the_direction_of_motion(self):
        from PIL import ImageStat

        with tempfile.TemporaryDirectory(prefix='lwe-trail-direction-') as directory:
            for fade in (False, True):
                with self.subTest(fade=fade):
                    root = Path(directory) / str(fade)
                    (root / 'particles').mkdir(parents=True)
                    (root / 'materials').mkdir()
                    # Gojo uses this stock asymmetric texture: its bright wide
                    # head is at V=0, with a narrow fading tail toward V=1.
                    (root / 'materials/probe.json').write_text(json.dumps({'passes': [{
                        'shader': 'genericparticle', 'textures': ['particle/light/light_shafts_0'],
                        'blending': 'additive', 'cullmode': 'nocull',
                        'depthtest': 'disabled', 'depthwrite': 'disabled'}]}))
                    (root / 'particles/probe.json').write_text(json.dumps({
                        'maxcount': 1, 'material': 'materials/probe.json',
                        'emitter': [{'name': 'sphererandom', 'rate': 100,
                                     'distancemin': 0, 'distancemax': 0}],
                        'initializer': [{'name': 'lifetimerandom', 'min': 10, 'max': 10},
                                        {'name': 'sizerandom', 'min': 32, 'max': 32},
                                        {'name': 'velocityrandom', 'min': '100 0 0', 'max': '100 0 0'}],
                        'operator': [{'name': 'movement', 'gravity': '0 0 0', 'drag': 0}],
                        'renderer': [{'name': 'ropetrail', 'length': .5,
                                      'fadealpha': fade, 'fadesize': fade}]}))
                    frame, _ = render_scene(self, root, {
                        'camera': {'eye': '0 0 1', 'center': '0 0 0', 'up': '0 1 0'},
                        'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                                    'clearcolor': '0 0 0', 'bloom': False},
                        'objects': [{'id': 1, 'name': 'Trail moving right', 'origin': '70 90 0',
                                     'particle': 'particles/probe.json'}]}, frames=15)
                    box = frame.getbbox()
                    self.assertIsNotNone(box)
                    left, top, right, bottom = box
                    self.assertGreater(right - left, 30, 'The probe must retain a visible trail')
                    third = (right - left) // 3
                    tail = ImageStat.Stat(frame.crop((left, top, left + third, bottom))).sum[0]
                    head = ImageStat.Stat(frame.crop((right - third, top, right, bottom))).sum[0]
                    self.assertGreater(head, tail * 3,
                                       'The bright end must move right with the particle, including fade flags')


if __name__ == '__main__':
    unittest.main()
