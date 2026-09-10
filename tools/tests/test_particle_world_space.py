"""World-space births retain their positions and use world units for forces/size."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_text_effect_targets import base_scene


def write_assets(root, flags, **changes):
    (root / 'particles').mkdir(parents=True)
    (root / 'materials').mkdir()
    (root / 'materials/probe.json').write_text(json.dumps({'passes': [{
        'shader': 'genericparticle', 'textures': ['util/white'],
        'blending': 'normal', 'cullmode': 'nocull',
        'depthtest': 'disabled', 'depthwrite': 'disabled'}]}))
    particle = {'flags': flags, 'maxcount': 1, 'material': 'materials/probe.json',
                'emitter': [{'name': 'sphererandom', 'instantaneous': 1, 'rate': 0,
                             'distancemin': 0, 'distancemax': 0}],
                'initializer': [{'name': 'lifetimerandom', 'min': 10, 'max': 10},
                                {'name': 'sizerandom', 'min': 20, 'max': 20}]}
    particle.update(changes)
    (root / 'particles/probe.json').write_text(json.dumps(particle))
    return particle


def center(frame):
    left, top, right, bottom = frame.getbbox()
    return ((left + right) / 2, (top + bottom) / 2)


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics tests')
class ParticleWorldSpace(unittest.TestCase):
    def test_world_births_keep_their_position_and_width_after_the_emitter_moves(self):
        with tempfile.TemporaryDirectory(prefix='lwe-world-birth-') as directory:
            for flags, x, width in ((0, 160, 30), (1, 80, 10)):
                with self.subTest(flags=flags):
                    root = Path(directory) / str(flags)
                    write_assets(root, flags)
                    frame, _ = render_scene(self, root, base_scene([{
                        'id': 1, 'name': 'Moving scaled emitter', 'particle': 'particles/probe.json',
                        'scale': '3 3 3', 'origin': {'value': '80 90 0', 'script': '''
let elapsed = 0;
export function update(value) {
    elapsed += engine.frametime;
    if (elapsed > .35) value.x = 160;
    return value;
}
'''}}]), frames=10)
                    self.assertIsNotNone(frame.getbbox())
                    self.assertAlmostEqual(center(frame)[0], x, delta=1)
                    self.assertAlmostEqual(frame.getbbox()[2] - frame.getbbox()[0], width, delta=1)

    def test_world_gravity_is_not_scaled_with_birth_velocity(self):
        with tempfile.TemporaryDirectory(prefix='lwe-world-gravity-') as directory:
            centers = []
            for flags in (0, 1):
                root = Path(directory) / str(flags)
                particle = write_assets(root, flags)
                particle['initializer'].append({'name': 'velocityrandom', 'min': '10 0 0', 'max': '10 0 0'})
                particle['operator'] = [{'name': 'movement', 'gravity': '0 -20 0', 'drag': 0}]
                (root / 'particles/probe.json').write_text(json.dumps(particle))
                frame, _ = render_scene(self, root, base_scene([{
                    'id': 1, 'name': 'Scaled meteor', 'particle': 'particles/probe.json',
                    'origin': '80 90 0', 'scale': '3 3 3'}]), frames=10)
                self.assertIsNotNone(frame.getbbox())
                centers.append(center(frame))
            local, world = centers
            self.assertGreater(world[0], 100, 'The emitter scale still affects the initial velocity')
            self.assertAlmostEqual(local[0], world[0], delta=2)
            self.assertGreater(abs(world[1] - 90), 4)
            self.assertAlmostEqual(local[1] - 90, (world[1] - 90) * 3, delta=2)

    def test_world_particles_use_world_control_points_for_forces(self):
        with tempfile.TemporaryDirectory(prefix='lwe-world-attract-') as directory:
            root = Path(directory)
            write_assets(root, 1, controlpoint=[{'id': 1, 'flags': 2, 'offset': '160 90 0'}],
                         operator=[{'name': 'movement', 'gravity': '0 0 0', 'drag': 0},
                                   {'name': 'controlpointattract', 'controlpoint': 1,
                                    'scale': 100, 'threshold': 200}])
            frame, _ = render_scene(self, root, base_scene([{
                'id': 1, 'name': 'Attracted world particle', 'particle': 'particles/probe.json',
                'origin': '80 90 0', 'scale': '2 2 2'}]), frames=10)
            self.assertIsNotNone(frame.getbbox())
            self.assertGreater(center(frame)[0], 95, 'The force must pull toward the actual world point')

    def test_follow_children_share_the_world_parent_path_without_double_translation(self):
        with tempfile.TemporaryDirectory(prefix='lwe-world-follow-') as directory:
            for child_flags in (0, 1):
                with self.subTest(child_flags=child_flags):
                    root = Path(directory) / str(child_flags)
                    parent = write_assets(root, 1)
                    child = dict(parent, flags=child_flags, maxcount=64,
                                 emitter=[{'name': 'sphererandom', 'rate': 30,
                                           'distancemin': 0, 'distancemax': 0}])
                    (root / 'particles/child.json').write_text(json.dumps(child))
                    parent['initializer'] = [*parent['initializer'],
                        {'name': 'alpharandom', 'min': 0, 'max': 0},
                        {'name': 'velocityrandom', 'min': '30 0 0', 'max': '30 0 0'}]
                    parent['operator'] = [{'name': 'movement', 'gravity': '0 0 0', 'drag': 0}]
                    parent['children'] = [{'name': 'particles/child.json', 'type': 'eventfollow', 'maxcount': 1}]
                    (root / 'particles/probe.json').write_text(json.dumps(parent))
                    frame, _ = render_scene(self, root, base_scene([{
                        'id': 1, 'name': 'World parent', 'particle': 'particles/probe.json',
                        'origin': '80 90 0', 'scale': '2 2 2'}]), frames=10)
                    box = frame.getbbox()
                    self.assertIsNotNone(box)
                    self.assertLess(box[0], 100, 'Released children must remain near the start of the path')
                    self.assertGreater(box[2], 130)
                    self.assertLess(box[2], 175, 'Parent translation must only be applied once')
                    self.assertAlmostEqual(center(frame)[1], 90, delta=1)


if __name__ == '__main__':
    unittest.main()
