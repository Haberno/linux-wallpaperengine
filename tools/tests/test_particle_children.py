"""Opt-in child particle rendering, hierarchy, tint and playback regressions."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticleChildren(unittest.TestCase):
    def test_nested_children_follow_transform_tint_visibility_and_playback(self):
        with tempfile.TemporaryDirectory(prefix='lwe-particle-children-') as directory:
            for mode in ('visible', 'hidden', 'paused', 'stopped'):
                with self.subTest(mode=mode):
                    root = Path(directory) / mode
                    (root / 'particles').mkdir(parents=True)
                    (root / 'materials').mkdir()
                    material = {'passes': [{
                        'shader': 'genericparticle', 'textures': ['util/white'],
                        'blending': 'normal', 'cullmode': 'nocull',
                        'depthtest': 'disabled', 'depthwrite': 'disabled'}]}
                    (root / 'materials/probe.json').write_text(json.dumps(material))
                    base = {'maxcount': 10, 'material': 'materials/probe.json'}
                    # Only the grandchild emits; paths use the authored name field.
                    (root / 'particles/root.json').write_text(json.dumps(dict(base, children=[{
                        'name': 'particles/middle.json', 'origin': '20 0 0'}])))
                    (root / 'particles/middle.json').write_text(json.dumps(dict(base, children=[{
                        'name': 'particles/leaf.json', 'origin': '20 0 0'}])))
                    leaf = dict(base, emitter=[{'name': 'sphererandom', 'rate': 20, 'controlpoint': -1,
                                               'distancemin': 0, 'distancemax': 0}],
                                initializer=[{'name': 'lifetimerandom', 'min': 10, 'max': 10},
                                             {'name': 'sizerandom', 'min': 10, 'max': 10}])
                    # Cycles must not recursively allocate forever or suppress the leaf.
                    leaf['children'] = [{'name': 'particles/root.json'}]
                    (root / 'particles/leaf.json').write_text(json.dumps(leaf))
                    origin = '160 50 0'
                    if mode in ('paused', 'stopped'):
                        origin = {'value': origin, 'script': 'export function init() { thisLayer.'
                                  + ('pause' if mode == 'paused' else 'stop') + '(); }'}
                    frame, log = render_scene(self, root, {
                        'camera': {'eye': '0 0 1', 'center': '0 0 0', 'up': '0 1 0'},
                        'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                                    'clearcolor': '0 0 0', 'bloom': False},
                        'objects': [{'id': 1, 'name': 'Root', 'particle': 'particles/root.json',
                                     'origin': origin, 'angles': '0 0 1.5707963',
                                     'visible': mode != 'hidden',
                                     'instanceoverride': {'colorn': '1 0.25 0.125', 'size': 2}}]})
                    if mode != 'visible':
                        self.assertIsNone(frame.getbbox(), log)
                        continue
                    box = frame.getbbox()
                    self.assertIsNotNone(box, 'A root with no emitter must still draw its descendants')
                    self.assertAlmostEqual((box[0] + box[2]) / 2, 160, delta=1)
                    self.assertAlmostEqual((box[1] + box[3]) / 2, 90, delta=1)
                    self.assertGreater(box[2] - box[0], 8, 'The root size override reaches grandchildren')
                    color = frame.getpixel((160, 90))
                    self.assertGreater(color[0], 240)
                    self.assertAlmostEqual(color[1], 64, delta=3)
                    self.assertAlmostEqual(color[2], 32, delta=3)
                    self.assertIn('recursive or excessive particle child', log)


if __name__ == '__main__':
    unittest.main()
