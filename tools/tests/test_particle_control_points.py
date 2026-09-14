"""Opt-in control-point coordinate and linkage regressions."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_text_effect_targets import base_scene


def write_particle(root, points, emitter_point=1):
    (root / 'particles').mkdir(parents=True)
    (root / 'materials').mkdir()
    (root / 'particles/probe.json').write_text(json.dumps({
        'maxcount': 1, 'material': 'materials/probe.json', 'controlpoint': points,
        'emitter': [{'name': 'sphererandom', 'rate': 100, 'controlpoint': emitter_point,
                     'distancemin': 0, 'distancemax': 0}],
        'initializer': [{'name': 'lifetimerandom', 'min': 10, 'max': 10},
                        {'name': 'sizerandom', 'min': 6, 'max': 6}],
        'renderer': [{'name': 'sprite'}]}))
    (root / 'materials/probe.json').write_text(json.dumps({'passes': [{
        'shader': 'genericparticle', 'textures': ['util/white'],
        'blending': 'normal', 'cullmode': 'nocull',
        'depthtest': 'disabled', 'depthwrite': 'disabled'}]}))


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticleControlPoints(unittest.TestCase):
    def assert_center(self, frame, x, y):
        box = frame.getbbox()
        self.assertIsNotNone(box, 'The particle must appear at its authored control point')
        self.assertAlmostEqual((box[0] + box[2]) / 2, x, delta=1)
        self.assertAlmostEqual((box[1] + box[3]) / 2, y, delta=1)

    def test_local_overrides_match_definition_offsets_on_mirrored_layers(self):
        from PIL import ImageChops

        with tempfile.TemporaryDirectory(prefix='lwe-cp-local-') as directory:
            frames = []
            for override in (False, True):
                root = Path(directory) / f'local-{override}'
                write_particle(root, [{'id': 1, 'flags': 0,
                                       'offset': '0 0 0' if override else '30 20 0'}])
                particle = {'id': 1, 'name': 'Mirrored emitter', 'origin': '160 90 0',
                            'scale': '-1 1 1', 'particle': 'particles/probe.json'}
                if override:
                    particle['instanceoverride'] = {'controlpoint1': '30 20 0'}
                frame, _ = render_scene(self, root, base_scene([particle]))
                self.assert_center(frame, 130, 70)
                frames.append(frame)
            self.assertIsNone(ImageChops.difference(*frames).getbbox())

    def test_world_points_use_the_complete_parent_transform(self):
        with tempfile.TemporaryDirectory(prefix='lwe-cp-world-') as directory:
            for override in (False, True):
                root = Path(directory) / f'world-{override}'
                write_particle(root, [{'id': 1, 'flags': 2,
                                       'offset': '0 0 0' if override else '140 100 0'}])
                particle = {'id': 2, 'name': 'Child emitter', 'parent': 1,
                            'origin': '10 0 0', 'scale': '-1 1 1',
                            'particle': 'particles/probe.json'}
                if override:
                    particle['instanceoverride'] = {'controlpoint1': '140 100 0'}
                frame, _ = render_scene(self, root, base_scene([
                    {'id': 1, 'name': 'Rotated parent', 'solid': True, 'origin': '160 90 0',
                     'angles': '0 0 1.570796327', 'scale': '2 1 1'}, particle]))
                self.assert_center(frame, 140, 80)

    def test_world_control_point_zero_positions_the_emitter(self):
        with tempfile.TemporaryDirectory(prefix='lwe-cp-zero-') as directory:
            root = Path(directory)
            write_particle(root, [{'id': 0, 'flags': 2, 'offset': '140 100 0'}], emitter_point=0)
            frame, _ = render_scene(self, root, base_scene([{
                'id': 1, 'name': 'Emitter origin', 'origin': '160 90 0',
                'particle': 'particles/probe.json'}]))
            self.assert_center(frame, 140, 80)

    def test_children_inherit_only_the_selected_parent_point(self):
        with tempfile.TemporaryDirectory(prefix='lwe-cp-child-') as directory:
            for flags, expected in ((0, (180, 90)), (4, (200, 70)), (12, (260, 50))):
                root = Path(directory) / f'child-{flags}'
                write_particle(root, [{'id': 1, 'flags': flags, 'parentcontrolpoint': 2,
                                       'offset': '0 0 0'}])
                (root / 'particles/root.json').write_text(json.dumps({
                    'maxcount': 1, 'material': 'materials/probe.json',
                    'controlpoint': [{'id': 2, 'flags': 0, 'offset': '40 20 0'}],
                    # Declaring a point does not allocate its native runtime slot.
                    'emitter': [{'name': 'boxrandom', 'rate': 0, 'controlpoint': 2}],
                    'children': [{'type': 'static', 'name': 'particles/probe.json',
                                  'origin': '20 0 0', 'scale': '2 2 1'}]}))
                frame, _ = render_scene(self, root, base_scene([{
                    'id': 1, 'name': 'Parent points', 'origin': '160 90 0',
                    'particle': 'particles/root.json'}]))
                self.assert_center(frame, *expected)


if __name__ == '__main__':
    unittest.main()
