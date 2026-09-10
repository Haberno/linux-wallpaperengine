"""Opt-in following child emitters: moving source, independent tails and retirement."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_text_effect_targets import base_scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticleFollow(unittest.TestCase):
    def test_large_first_child_pool_does_not_discard_its_siblings(self):
        with tempfile.TemporaryDirectory(prefix='lwe-follow-siblings-') as directory:
            root = Path(directory)
            (root / 'particles').mkdir()
            (root / 'materials').mkdir()
            (root / 'materials/probe.json').write_text(json.dumps({'passes': [{
                'shader': 'genericparticle', 'textures': ['util/white'],
                'blending': 'translucent', 'cullmode': 'nocull',
                'depthtest': 'disabled', 'depthwrite': 'disabled'}]}))
            base = {'maxcount': 100, 'material': 'materials/probe.json',
                    'emitter': [{'name': 'sphererandom', 'instantaneous': 1, 'rate': 0,
                                 'distancemin': 0, 'distancemax': 0}],
                    'initializer': [{'name': 'lifetimerandom', 'min': 10, 'max': 10},
                                    {'name': 'sizerandom', 'min': 10, 'max': 10}]}
            for index, position in enumerate((-40, 40)):
                child = dict(base, emitter=[dict(base['emitter'][0], origin=f'{position} 0 0')])
                (root / f'particles/child{index}.json').write_text(json.dumps(child))
            parent = dict(base, initializer=[*base['initializer'],
                          {'name': 'alpharandom', 'min': 0, 'max': 0}], children=[
                {'name': f'particles/child{index}.json', 'type': 'eventfollow', 'maxcount': maximum}
                for index, maximum in enumerate((100, 40))])
            (root / 'particles/root.json').write_text(json.dumps(parent))
            frame, _ = render_scene(self, root, base_scene([{
                'id': 1, 'name': 'Parent', 'particle': 'particles/root.json', 'origin': '160 90 0'}]))
            self.assertGreater(min(frame.getpixel((120, 90))), 240)
            self.assertGreater(min(frame.getpixel((200, 90))), 240)

    def test_children_follow_births_and_finish_after_the_parent_dies(self):
        with tempfile.TemporaryDirectory(prefix='lwe-particle-follow-') as directory:
            for label, lifetime, child_lifetime, probability, visible in (
                    ('moving', 2, 1, 1, True), ('expired', .3, .2, 1, False),
                    ('probability-zero', 2, 1, 0, False)):
                with self.subTest(label=label):
                    root = Path(directory) / label
                    (root / 'particles').mkdir(parents=True)
                    (root / 'materials').mkdir()
                    (root / 'materials/probe.json').write_text(json.dumps({'passes': [{
                        'shader': 'genericparticle', 'textures': ['util/white'],
                        'blending': 'translucent', 'cullmode': 'nocull',
                        'depthtest': 'disabled', 'depthwrite': 'disabled'}]}))
                    (root / 'particles/root.json').write_text(json.dumps({
                        'maxcount': 1, 'material': 'materials/probe.json',
                        'emitter': [{'name': 'sphererandom', 'instantaneous': 1,
                                     'rate': 0, 'distancemin': 0, 'distancemax': 0}],
                        'initializer': [{'name': 'lifetimerandom', 'min': lifetime, 'max': lifetime},
                                        {'name': 'alpharandom', 'min': 0, 'max': 0},
                                        {'name': 'velocityrandom', 'min': '100 0 0', 'max': '100 0 0'}],
                        'operator': [{'name': 'movement', 'drag': 0, 'gravity': '0 0 0'}],
                        'children': [{'name': 'particles/child.json', 'type': 'eventfollow',
                                      'maxcount': 1, 'probability': probability}]}))
                    (root / 'particles/child.json').write_text(json.dumps({
                        'maxcount': 64, 'material': 'materials/probe.json',
                        'emitter': [{'name': 'sphererandom', 'rate': 30,
                                     'distancemin': 0, 'distancemax': 0}],
                        'initializer': [{'name': 'lifetimerandom', 'min': child_lifetime, 'max': child_lifetime},
                                        {'name': 'sizerandom', 'min': 8, 'max': 8}]}))
                    frame, _ = render_scene(self, root, base_scene([{
                        'id': 1, 'name': 'Moving parent', 'particle': 'particles/root.json',
                        'origin': '80 90 0'}]), frames=10)
                    box = frame.getbbox()
                    self.assertEqual(box is not None, visible)
                    if visible:
                        self.assertGreater(box[2] - box[0], 45,
                                           'Released child particles must stay behind the moving emitter')
                        self.assertGreater(box[0], 85, 'Children must emit along the parent particle path')
                        self.assertLess(box[2], 190, 'Children must not receive parent motion twice')


if __name__ == '__main__':
    unittest.main()
